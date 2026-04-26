#!/usr/bin/env python3
from __future__ import annotations

"""Self-contained performance runner with native scenario implementations.

This file intentionally does not call old load scenario modules.
All scenario logic is defined directly here.
"""

import argparse
import contextlib
import concurrent.futures
import errno
import hashlib
import importlib.util
import ipaddress
import json
import math
import os
import socket
import sys
import time
import uuid
from collections import Counter
from dataclasses import asdict
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path


RUNNER_DIR = Path(__file__).parent.resolve()
TEST_DIR = RUNNER_DIR.parent
DEFAULT_RESULTS_FILE = TEST_DIR / "performance_test_results.json"

TRACE_LEVEL_ENV = "MQTT_INTEGRATION_TRACE_LEVEL"
TRACE_MODULES_ENV = "MQTT_INTEGRATION_TRACE_MODULES"
BROKER_OUTPUT_ENV = "MQTT_INTEGRATION_BROKER_OUTPUT"
TARGET_HOST_ENV = "MQTT_INTEGRATION_TARGET_HOST"
TARGET_PORT_ENV = "MQTT_INTEGRATION_TARGET_PORT"
TARGET_WS_PORT_ENV = "MQTT_INTEGRATION_TARGET_WS_PORT"

DEFAULT_REMOTE_WS_PORT = 8083
PERFORMANCE_CLIENT_MAX_INFLIGHT = 50
PERFORMANCE_CLIENT_SOCKET_SNDBUF = 16 * 1024 * 1024
PERFORMANCE_CLIENT_SOCKET_RCVBUF = 16 * 1024 * 1024
PERFORMANCE_CLIENT_TCP_NODELAY = True


@dataclass(frozen=True)
class RunnerConfig:
    host: str
    port: int
    ws_port: int | None
    timeout_seconds: float
    size_profile: str
    duration_seconds: float


@dataclass(frozen=True)
class StartupOptions:
    trace_level: str | None
    trace_modules: tuple[str, ...]
    broker_output_inherit: bool


@dataclass(frozen=True)
class ScenarioSpec:
    stage_id: int
    scenario_id: str
    title: str
    purpose: str
    success_limit: str
    work_units: float | None
    work_unit_label: str | None


@dataclass(frozen=True)
class ScenarioOutcome:
    success: bool
    summary: str
    counters: dict[str, int]
    work_units: float | None
    throughput_per_second: float | None = None
    throughput_basis_seconds: float | None = None


@dataclass(frozen=True)
class ScenarioResult:
    stage_id: int
    scenario_id: str
    run_index: int
    title: str
    status: str
    duration_seconds: float
    throughput_per_second: float | None
    summary: str
    counters: dict[str, int]
    started_at: str
    finished_at: str


@dataclass(frozen=True)
class ScenarioDef:
    spec: ScenarioSpec
    execute: callable


SCENARIO_SPECS: tuple[ScenarioSpec, ...] = (
    ScenarioSpec(
        1,
        "S01",
        "connect-ramp-two-minute",
        "2-minute connect load with hold phase and accelerated ramp",
        "range by size: small 10-100, middle 50-1000, large 500-10000",
        None,
        None,
    ),
    ScenarioSpec(11, "P01", "qos0-message-rate-ramp", "QoS0 throughput with increasing message rate", ">=99% delivered in hold phase", 1.0, "msg"),
    ScenarioSpec(12, "P02", "qos1-message-rate-ramp", "QoS1 throughput with PUBACK latency", ">=99% delivered and >=99% PUBACK in hold", 1.0, "msg"),
    ScenarioSpec(13, "P03", "qos2-message-rate-ramp", "QoS2 exactly-once handshake throughput", ">=99% PUBCOMP and deliveries in hold", 1.0, "msg"),
    ScenarioSpec(14, "P04", "subscriber-fanout-ramp", "One publisher with increasing subscriber fan-out", ">=99% of expected deliveries in hold", 1.0, "delivery"),
    ScenarioSpec(15, "P05", "publisher-fanin-ramp", "Increasing concurrent publishers to one subscriber", ">=99% delivered in hold", 1.0, "msg"),
    ScenarioSpec(16, "P06", "large-payload-throughput-ramp", "Payload size ramp and peak-size burst", "all hold burst messages delivered in timeout", 1.0, "byte"),
    ScenarioSpec(17, "P07", "retained-replay-ramp", "Retained replay latency under topic growth", ">=99% retained replay per hold iteration", 1.0, "entry"),
    ScenarioSpec(18, "P08", "offline-queue-pressure-ramp", "QoS1 offline queue growth and reconnect drain", "all queued messages drained in hold", 1.0, "msg"),
    ScenarioSpec(19, "P09", "subscription-churn-ramp", "Subscribe/unsubscribe churn under increasing clients", "stable delivery ratio >=99% in hold", 1.0, "msg"),
    ScenarioSpec(20, "P10", "will-delivery-ramp", "Will delivery under abrupt disconnect ramp", ">=99% will messages observed in hold", 1.0, "will"),
)


SIZE_STAGE_RANGES: dict[str, tuple[int, int]] = {
    "small": (1, 13),
    "middle": (1, 17),
    "large": (1, 20),
}


PARAMS_P01 = {
    "small": {"start_msg_s": 10, "end_msg_s": 500, "step": 10, "ramp_interval_s": 3.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 200, "end_msg_s": 2000, "step": 50, "ramp_interval_s": 2.0, "hold_seconds": 30},
    "large": {"start_msg_s": 1000, "end_msg_s": 10000, "step": 200, "ramp_interval_s": 1.0, "hold_seconds": 30},
}

PARAMS_P02 = {
    "small": {"start_msg_s": 1, "end_msg_s": 50, "step": 1, "ramp_interval_s": 10.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 20, "end_msg_s": 200, "step": 20, "ramp_interval_s": 10.0, "hold_seconds": 30},
    "large": {"start_msg_s": 100, "end_msg_s": 1000, "step": 100, "ramp_interval_s": 10.0, "hold_seconds": 30},
}

PARAMS_P03 = {
    "small": {"start_msg_s": 5, "end_msg_s": 50, "step": 5, "ramp_interval_s": 4.0, "hold_seconds": 30},
    "middle": {"start_msg_s": 20, "end_msg_s": 500, "step": 20, "ramp_interval_s": 2.0, "hold_seconds": 30},
    "large": {"start_msg_s": 50, "end_msg_s": 3000, "step": 50, "ramp_interval_s": 1.5, "hold_seconds": 30},
}

PARAMS_P04 = {
    "small": {"start_subscribers": 1, "end_subscribers": 10, "step": 1, "ramp_interval_s": 5.0, "msg_per_step": 20, "hold_seconds": 30, "qos": 0},
    "middle": {"start_subscribers": 1, "end_subscribers": 50, "step": 5, "ramp_interval_s": 3.0, "msg_per_step": 50, "hold_seconds": 30, "qos": 0},
    "large": {"start_subscribers": 5, "end_subscribers": 200, "step": 10, "ramp_interval_s": 2.0, "msg_per_step": 100, "hold_seconds": 30, "qos": 0},
}

PARAMS_P05 = {
    "small": {"start_publishers": 1, "end_publishers": 10, "step": 1, "ramp_interval_s": 5.0, "msg_per_publisher_per_step": 10, "hold_seconds": 30, "qos": 0},
    "middle": {"start_publishers": 1, "end_publishers": 50, "step": 5, "ramp_interval_s": 3.0, "msg_per_publisher_per_step": 20, "hold_seconds": 30, "qos": 0},
    "large": {"start_publishers": 5, "end_publishers": 200, "step": 10, "ramp_interval_s": 2.0, "msg_per_publisher_per_step": 30, "hold_seconds": 30, "qos": 0},
}

PARAMS_P06 = {
    "small": {"start_payload_bytes": 64, "end_payload_bytes": 8192, "step_bytes": 512, "ramp_interval_s": 2.0, "burst_count": 50, "hold_seconds": 20, "qos": 1},
    "middle": {"start_payload_bytes": 128, "end_payload_bytes": 65536, "step_bytes": 4096, "ramp_interval_s": 2.0, "burst_count": 100, "hold_seconds": 20, "qos": 1},
    "large": {"start_payload_bytes": 512, "end_payload_bytes": 262144, "step_bytes": 16384, "ramp_interval_s": 2.0, "burst_count": 200, "hold_seconds": 20, "qos": 1},
}

PARAMS_P07 = {
    "small": {"start_topics": 10, "end_topics": 200, "step": 10, "ramp_interval_s": 2.0, "hold_seconds": 20, "qos": 0},
    "middle": {"start_topics": 50, "end_topics": 1000, "step": 50, "ramp_interval_s": 2.0, "hold_seconds": 20, "qos": 0},
    "large": {"start_topics": 200, "end_topics": 5000, "step": 200, "ramp_interval_s": 1.5, "hold_seconds": 20, "qos": 0},
}

PARAMS_P08 = {
    "small": {"start_queued": 10, "end_queued": 100, "step": 10, "ramp_interval_s": 3.0, "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 30},
    "middle": {"start_queued": 50, "end_queued": 500, "step": 50, "ramp_interval_s": 3.0, "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 30},
    "large": {"start_queued": 100, "end_queued": 2000, "step": 100, "ramp_interval_s": 2.0, "hold_seconds": 20, "session_expiry_s": 300, "drain_timeout_s": 60},
}

PARAMS_P09 = {
    "small": {"start_clients": 2, "end_clients": 10, "step": 1, "ramp_interval_s": 5.0, "filters_per_client": 10, "churn_interval_s": 0.5, "hold_seconds": 30},
    "middle": {"start_clients": 5, "end_clients": 50, "step": 5, "ramp_interval_s": 3.0, "filters_per_client": 20, "churn_interval_s": 0.3, "hold_seconds": 30},
    "large": {"start_clients": 10, "end_clients": 200, "step": 10, "ramp_interval_s": 2.0, "filters_per_client": 30, "churn_interval_s": 0.2, "hold_seconds": 30},
}

PARAMS_P10 = {
    "small": {"start_clients": 2, "end_clients": 20, "step": 2, "ramp_interval_s": 4.0, "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
    "middle": {"start_clients": 5, "end_clients": 100, "step": 5, "ramp_interval_s": 3.0, "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
    "large": {"start_clients": 10, "end_clients": 400, "step": 10, "ramp_interval_s": 2.0, "hold_seconds": 30, "will_qos": 1, "will_delay_s": 0},
}


def _planned_hold_seconds(config: RunnerConfig, base_hold_seconds: float) -> float:
    return max(0.0, float(base_hold_seconds) + max(0.0, config.duration_seconds - 120.0))


def _planned_ramp_duration_seconds(start_value: int, end_value: int, step_value: int, ramp_interval_seconds: float) -> float:
    step_count = len(list(_ramp_steps(int(start_value), int(end_value), int(step_value))))
    return float(step_count) * float(ramp_interval_seconds)


def _planned_ramp_duration_rate_scenario_seconds(
    start_value: int,
    end_value: int,
    step_value: int,
    ramp_interval_seconds: float,
) -> float:
    # Rate scenarios run each ramp level as a paced constant-load window.
    step_count = len(list(_ramp_steps(int(start_value), int(end_value), int(step_value))))
    return float(step_count) * float(ramp_interval_seconds)


def _planned_duration_seconds_for_scenario(config: RunnerConfig, scenario_id: str) -> float | None:
    if scenario_id == "S01":
        return 120.0
    if scenario_id in {"P01", "P02"}:
        return 120.0

    params_map = {
        "P02": PARAMS_P02,
        "P03": PARAMS_P03,
        "P04": PARAMS_P04,
        "P05": PARAMS_P05,
        "P06": PARAMS_P06,
        "P07": PARAMS_P07,
        "P08": PARAMS_P08,
        "P09": PARAMS_P09,
        "P10": PARAMS_P10,
    }
    params = params_map.get(scenario_id)
    if params is None:
        return None

    profile_params = params[config.size_profile]
    hold_seconds = _planned_hold_seconds(config, float(profile_params["hold_seconds"]))

    if scenario_id in {"P01", "P02", "P03"}:
        ramp_seconds = _planned_ramp_duration_rate_scenario_seconds(
            int(profile_params["start_msg_s"]),
            int(profile_params["end_msg_s"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P04":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_subscribers"]),
            int(profile_params["end_subscribers"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P05":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_publishers"]),
            int(profile_params["end_publishers"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P06":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_payload_bytes"]),
            int(profile_params["end_payload_bytes"]),
            int(profile_params["step_bytes"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P07":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_topics"]),
            int(profile_params["end_topics"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P08":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_queued"]),
            int(profile_params["end_queued"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P09":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_clients"]),
            int(profile_params["end_clients"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    elif scenario_id == "P10":
        ramp_seconds = _planned_ramp_duration_seconds(
            int(profile_params["start_clients"]),
            int(profile_params["end_clients"]),
            int(profile_params["step"]),
            float(profile_params["ramp_interval_s"]),
        )
    else:
        return None

    return ramp_seconds + hold_seconds


def _planned_total_duration_seconds(selected: list[ScenarioDef], config: RunnerConfig, repeat: int) -> float | None:
    total = 0.0
    for definition in selected:
        planned = _planned_duration_seconds_for_scenario(config, definition.spec.scenario_id)
        if planned is None:
            return None
        total += planned * float(repeat)
    return total


def _now_utc_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _compact(text: str) -> str:
    return " ".join(text.strip().split())


def _clip(text: str, width: int = 78) -> str:
    value = _compact(text)
    if len(value) <= width:
        return value
    return value[: width - 3] + "..."


def _is_ip_literal(hostname: str) -> bool:
    try:
        ipaddress.ip_address(hostname.strip())
        return True
    except ValueError:
        return False


def _is_local_host(hostname: str) -> bool:
    normalized = hostname.strip().lower()
    return normalized in {"localhost", "127.0.0.1", "::1"} or normalized.startswith("127.")


def _broker_reachable(hostname: str, port: int, timeout_seconds: float = 0.5) -> bool:
    try:
        with socket.create_connection((hostname, port), timeout=timeout_seconds):
            return True
    except OSError:
        return False


def _resolve_runner_target_host(hostname: str, port: int) -> tuple[str, str | None]:
    normalized_host = hostname.strip()
    if not normalized_host or _is_ip_literal(normalized_host):
        return normalized_host, None

    try:
        infos = socket.getaddrinfo(normalized_host, port, type=socket.SOCK_STREAM)
    except OSError:
        return normalized_host, None

    candidates: list[tuple[int, str]] = []
    for family, sock_type, _protocol, _, sockaddr in infos:
        if sock_type != socket.SOCK_STREAM:
            continue
        address = sockaddr[0]
        if any(existing == address for _, existing in candidates):
            continue
        candidates.append((family, address))

    if not candidates:
        return normalized_host, None

    for _family, address in candidates:
        if _broker_reachable(address, port, timeout_seconds=0.4):
            if address != normalized_host:
                return address, f"resolved host {normalized_host} -> {address}"
            return address, None

    ipv4 = next((address for family, address in candidates if family == socket.AF_INET), None)
    if ipv4 is not None and ipv4 != normalized_host:
        return ipv4, f"resolved host {normalized_host} -> {ipv4} (ipv4 fallback)"

    return normalized_host, None


def _preflight_target_connectivity(config: RunnerConfig) -> tuple[bool, str | None]:
    try:
        with socket.create_connection((config.host, config.port), timeout=1.2):
            return True, None
    except OSError as error:
        if getattr(error, "errno", None) == errno.ECONNREFUSED:
            return False, f"target {config.host}:{config.port} refused connection"
        return False, f"target {config.host}:{config.port} not reachable: {error}"


def _load_helper(module_name: str):
    helper_path = TEST_DIR / "integration_tests" / "helpers" / f"{module_name}.py"
    digest = hashlib.sha1(str(helper_path).encode("utf-8")).hexdigest()[:12]
    spec = importlib.util.spec_from_file_location(f"perf_helper_{module_name}_{digest}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_mqtt_client_module = _load_helper("mqtt_client")
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties


def _connect_client(
    config: RunnerConfig,
    client_prefix: str,
    *,
    timeout: float | None = None,
    client_id: str | None = None,
    clean_start: bool = True,
    properties: Properties | None = None,
) -> MqttClient:
    resolved_timeout = float(config.timeout_seconds if timeout is None else timeout)
    resolved_client_id = client_id if client_id is not None else f"{client_prefix}-{_unique_suffix()}"
    client = MqttClient(
        client_id=resolved_client_id,
        timeout_seconds=resolved_timeout,
        max_inflight_messages=PERFORMANCE_CLIENT_MAX_INFLIGHT,
        socket_send_buffer_bytes=PERFORMANCE_CLIENT_SOCKET_SNDBUF,
        socket_receive_buffer_bytes=PERFORMANCE_CLIENT_SOCKET_RCVBUF,
        socket_tcp_nodelay=PERFORMANCE_CLIENT_TCP_NODELAY,
    )
    client.connect(
        config.host,
        config.port,
        client_id=resolved_client_id,
        clean_start=clean_start,
        properties=properties,
    )
    return client


def _unique_suffix() -> str:
    return uuid.uuid4().hex[:10]


def _encode_varint(value: int) -> bytes:
    encoded = bytearray()
    remaining = int(value)
    while True:
        digit = remaining % 128
        remaining //= 128
        if remaining > 0:
            digit |= 0x80
        encoded.append(digit)
        if remaining == 0:
            break
    return bytes(encoded)


def _encode_utf8(text: str) -> bytes:
    payload = text.encode("utf-8")
    return len(payload).to_bytes(2, byteorder="big") + payload


def _build_connect_packet(client_id: str) -> bytes:
    variable_header = _encode_utf8("MQTT") + b"\x05\x02\x00\x1e" + b"\x00"
    payload = _encode_utf8(client_id)
    remaining = _encode_varint(len(variable_header) + len(payload))
    return b"\x10" + remaining + variable_header + payload


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        piece = sock.recv(size - len(chunks))
        if not piece:
            raise RuntimeError("connection closed while receiving packet")
        chunks.extend(piece)
    return bytes(chunks)


def _decode_remaining_length(sock: socket.socket) -> int:
    value = 0
    multiplier = 1
    for _ in range(4):
        byte_value = _recv_exact(sock, 1)[0]
        value += (byte_value & 0x7F) * multiplier
        if (byte_value & 0x80) == 0:
            return value
        multiplier *= 128
    raise RuntimeError("invalid remaining length")


def _read_connack_reason(sock: socket.socket) -> int:
    fixed = _recv_exact(sock, 1)[0]
    packet_type = (fixed >> 4) & 0x0F
    if packet_type != 2:
        raise RuntimeError(f"expected CONNACK packet, got type={packet_type}")
    remaining = _decode_remaining_length(sock)
    payload = _recv_exact(sock, remaining)
    if len(payload) < 2:
        raise RuntimeError("invalid CONNACK payload")
    return int(payload[1])


def _tcp_connect_trial(config: RunnerConfig, client_name: str) -> tuple[str, str]:
    tcp_socket: socket.socket | None = None
    try:
        tcp_socket = socket.create_connection((config.host, config.port), timeout=config.timeout_seconds)
        tcp_socket.settimeout(config.timeout_seconds)
        tcp_socket.sendall(_build_connect_packet(client_name))
        reason = _read_connack_reason(tcp_socket)
        if reason == 0:
            return "success", ""
        return "rejected", f"connack_reason={reason}"
    except OSError as error:
        if error.errno in {errno.EMFILE, errno.ENFILE}:
            return "local_resource", str(error)
        if error.errno in {errno.ECONNRESET, errno.EPIPE, errno.ECONNREFUSED, errno.ETIMEDOUT}:
            return "network_close", str(error)
        return "other", str(error)
    except BaseException as error:  # pylint: disable=broad-except
        return "other", str(error)
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass


def _open_one_persistent_connection(config: RunnerConfig, client_name: str, timeout_seconds: float) -> tuple[str, socket.socket | None, float]:
    tcp_socket: socket.socket | None = None
    started = time.perf_counter()
    try:
        tcp_socket = socket.create_connection((config.host, config.port), timeout=timeout_seconds)
        tcp_socket.settimeout(timeout_seconds)
        tcp_socket.sendall(_build_connect_packet(client_name))
        reason = _read_connack_reason(tcp_socket)
        if reason != 0:
            try:
                tcp_socket.close()
            except OSError:
                pass
            return "rejected", None, time.perf_counter() - started
        return "success", tcp_socket, time.perf_counter() - started
    except OSError as error:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        if error.errno in {errno.EMFILE, errno.ENFILE}:
            return "local_resource", None, time.perf_counter() - started
        if error.errno in {errno.ECONNRESET, errno.EPIPE, errno.ECONNREFUSED, errno.ETIMEDOUT}:
            return "network_close", None, time.perf_counter() - started
        return "other", None, time.perf_counter() - started
    except BaseException:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        return "other", None, time.perf_counter() - started


def _close_sockets(sockets: list[socket.socket]) -> int:
    closed = 0
    for tcp_socket in sockets:
        try:
            tcp_socket.close()
            closed += 1
        except OSError:
            pass
    return closed


def _run_connection_wave(
    config: RunnerConfig,
    *,
    requested_connections: int,
    submit_gap_seconds: float,
    connect_timeout_seconds: float,
    stop_deadline_monotonic: float | None = None,
) -> tuple[dict[str, int], list[socket.socket], float, float]:
    counters = {
        "requested": 0,
        "success": 0,
        "rejected": 0,
        "network_close": 0,
        "local_resource": 0,
        "other": 0,
    }
    active_sockets: list[socket.socket] = []

    max_workers = max(1, min(requested_connections, 256))
    started = time.perf_counter()
    submit_started = started
    next_submit_at = started
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures: list[concurrent.futures.Future[tuple[str, socket.socket | None, float]]] = []
        for index in range(requested_connections):
            if stop_deadline_monotonic is not None and time.monotonic() >= stop_deadline_monotonic:
                break
            now = time.perf_counter()
            wait_seconds = max(0.0, next_submit_at - now)
            if wait_seconds > 0.0:
                if stop_deadline_monotonic is not None:
                    remaining_until_deadline = stop_deadline_monotonic - time.monotonic()
                    if remaining_until_deadline <= 0.0:
                        break
                    wait_seconds = min(wait_seconds, max(0.0, remaining_until_deadline))
                time.sleep(wait_seconds)
                if stop_deadline_monotonic is not None and time.monotonic() >= stop_deadline_monotonic:
                    break
            future = executor.submit(
                _open_one_persistent_connection,
                config,
                f"s01-{index}-{_unique_suffix()}",
                connect_timeout_seconds,
            )
            futures.append(future)
            counters["requested"] += 1
            if submit_gap_seconds > 0.0:
                next_submit_at += submit_gap_seconds

        submit_finished = time.perf_counter()

        for future in concurrent.futures.as_completed(futures):
            category, maybe_socket, _duration = future.result()
            if category not in counters:
                category = "other"
            counters[category] += 1
            if maybe_socket is not None:
                active_sockets.append(maybe_socket)

    wave_duration = time.perf_counter() - started
    submit_duration = max(1e-6, submit_finished - submit_started)
    return counters, active_sockets, wave_duration, submit_duration


def _effective_hold_seconds(config: RunnerConfig, base_hold_seconds: float) -> float:
    return max(0.0, float(base_hold_seconds) + max(0.0, config.duration_seconds - 120.0))


def _ramp_steps(start_value: int, end_value: int, step_value: int):
    if step_value <= 0:
        raise ValueError("step must be > 0")
    current = int(start_value)
    final = int(end_value)
    while current < final:
        yield current
        current += int(step_value)
    yield final


def _drain_until(subscriber: MqttClient, expected_count: int, timeout_seconds: float) -> int:
    if expected_count <= 0:
        return 0
    received = 0
    deadline = time.monotonic() + max(0.1, timeout_seconds)
    while received < expected_count and time.monotonic() < deadline:
        remaining = expected_count - received
        drained = subscriber.drain_available_messages(limit=remaining)
        if drained:
            received += len(drained)
            continue
        time.sleep(0.005)
    return received


def _abrupt_disconnect(client: MqttClient) -> None:
    internal = getattr(client, "_client", None)
    if internal is None:
        return
    try:
        socket_object = internal.socket()
        if socket_object is not None:
            socket_object.close()
    except BaseException:
        pass
    try:
        internal.loop_stop()
    except BaseException:
        pass


def _scenario_step_logger(scenario_id: str):
    step_index = 1

    def _step(message: str) -> None:
        nonlocal step_index
        print(f"  [{scenario_id} STEP {step_index:02d}] {message}")
        step_index += 1

    return _step


def _progress_checkpoints(total_items: int) -> set[int]:
    if total_items <= 0:
        return set()
    return {
        1,
        max(1, math.ceil(total_items * 0.25)),
        max(1, math.ceil(total_items * 0.50)),
        max(1, math.ceil(total_items * 0.75)),
        total_items,
    }


def _safe_rate(work_units: float, elapsed_seconds: float) -> float:
    return float(work_units) / max(1e-6, float(elapsed_seconds))


def _format_p02_publish_abort_summary(
    error: RuntimeError,
    *,
    publisher: MqttClient,
    publisher_index: int,
    publisher_clients_total: int,
    publisher_sent_count: int,
    publisher_rate_cap_per_second: int,
    subscriber: MqttClient,
    sent_total: int,
    ack_total: int,
    received_total: int,
    pending_publish_count: int,
    current_rate: float,
) -> str:
    error_text = str(error)
    publisher_diag = publisher.runtime_diagnostics()
    subscriber_diag = subscriber.runtime_diagnostics()

    queue_hint = "unknown"
    if "rc=15" in error_text:
        queue_hint = "publisher-client-outgoing-queue"

    return (
        "p02_publish_abort"
        f" queue_hint={queue_hint}"
        f" error={_compact(error_text)}"
        f" pub_index={publisher_index + 1}/{publisher_clients_total}"
        f" pub_client_sent={publisher_sent_count}"
        f" pub_client_rate_cap_per_s={publisher_rate_cap_per_second}"
        f" pub_connected={int(bool(publisher_diag.get('connected', False)))}"
        f" pub_disconnect_source={publisher_diag.get('disconnect_source', 'none')}"
        f" pub_disconnect_reason={int(publisher_diag.get('disconnect_reason', -1))}"
        f" pub_out_messages={int(publisher_diag.get('out_messages', -1))}"
        f" pub_max_queued={int(publisher_diag.get('max_queued_messages', -1))}"
        f" pub_max_inflight={int(publisher_diag.get('max_inflight_messages', -1))}"
        f" sub_connected={int(bool(subscriber_diag.get('connected', False)))}"
        f" sub_inbound_queue={int(subscriber_diag.get('inbound_queue_depth', -1))}"
        f" stats_sent={sent_total} stats_acked={ack_total}"
        f" stats_received={received_total}"
        f" stats_pending_publish_mids={pending_publish_count}"
        f" stats_current_rate={current_rate:.2f}/s"
    )


def _drain_matching_messages(
    subscriber: MqttClient,
    expected_prefix: str,
    valid_ids: set[int],
    drain_seconds: float,
) -> int:
    delivered = 0
    deadline = time.monotonic() + max(0.01, float(drain_seconds))
    while time.monotonic() < deadline and valid_ids:
        drained = subscriber.drain_available_messages(limit=2048)
        if not drained:
            time.sleep(0.002)
            continue
        for message in drained:
            try:
                text = bytes(message.payload).decode("utf-8", errors="ignore")
            except BaseException:
                continue
            if not text.startswith(expected_prefix):
                continue
            parts = text.split(":", 1)
            if len(parts) != 2:
                continue
            try:
                seq = int(parts[1])
            except ValueError:
                continue
            if seq in valid_ids:
                valid_ids.remove(seq)
                delivered += 1
    return delivered


def _run_paced_window_qos0(
    publisher: MqttClient,
    subscriber: MqttClient,
    topic: str,
    *,
    target_msgs_per_second: int,
    window_seconds: float,
    sequence_prefix: str,
    next_sequence: int,
) -> tuple[int, int, int, float, float]:
    target_rate = max(1, int(target_msgs_per_second))
    duration = max(0.1, float(window_seconds))
    send_target = max(1, int(round(target_rate * duration)))

    sent_ids: set[int] = set()
    send_started = time.monotonic()
    send_deadline = send_started + duration
    sent = 0

    for index in range(send_target):
        scheduled = send_started + (float(index) / float(target_rate))
        now = time.monotonic()
        if scheduled > now:
            time.sleep(scheduled - now)
        payload = f"{sequence_prefix}:{next_sequence}".encode("utf-8")
        publisher.publish(topic, payload, qos=0)
        sent_ids.add(next_sequence)
        next_sequence += 1
        sent += 1

    send_elapsed = max(1e-6, time.monotonic() - send_started)
    remaining_window = max(0.0, send_deadline - time.monotonic())
    if remaining_window > 0.0:
        time.sleep(remaining_window)

    drain_budget = max(0.2, duration * 0.35)
    drain_started = time.monotonic()
    delivered = _drain_matching_messages(subscriber, sequence_prefix, sent_ids, drain_budget)
    drain_elapsed = max(1e-6, time.monotonic() - drain_started)
    return sent, delivered, next_sequence, send_elapsed, drain_elapsed


# Scenario implementations moved to test/performance_runner/scenarios/*.py

def _scenario_definitions() -> tuple[ScenarioDef, ...]:
    from .scenarios import p01
    from .scenarios import p02
    from .scenarios import p03
    from .scenarios import p04
    from .scenarios import p05
    from .scenarios import p06
    from .scenarios import p07
    from .scenarios import p08
    from .scenarios import p09
    from .scenarios import p10
    from .scenarios import s01

    definitions = {
        item.spec.scenario_id: item
        for item in (
            s01.definition(),
            p01.definition(),
            p02.definition(),
            p03.definition(),
            p04.definition(),
            p05.definition(),
            p06.definition(),
            p07.definition(),
            p08.definition(),
            p09.definition(),
            p10.definition(),
        )
    }
    return tuple(definitions[spec.scenario_id] for spec in SCENARIO_SPECS)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run yahabroker performance scenarios")
    parser.add_argument("--host", default="127.0.0.1", help="Broker hostname (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883, help="Broker TCP port (default: 1883)")
    parser.add_argument("--ws-port", type=int, default=None, help="Broker WebSocket port")
    parser.add_argument("--timeout", type=float, default=8.0, help="Base timeout in seconds")
    parser.add_argument("--duration", type=float, default=120.0, help="Scenario duration in seconds (default: 120)")
    parser.add_argument("--size", choices=["small", "middle", "large"], default="small", help="Profile")
    parser.add_argument("--repeat", type=int, default=1, help="Repeat each scenario")
    parser.add_argument("--filter", action="append", default=[], help="Scenario ID, stage, or title prefix")
    parser.add_argument("--list", action="store_true", help="List scenarios")
    parser.add_argument("--results-file", default=str(DEFAULT_RESULTS_FILE), help="Results output file")
    parser.add_argument("--trace-level", choices=["none", "error", "warning", "info", "trace"], default=None)
    parser.add_argument("--trace-module", action="append", default=[])
    parser.add_argument("--broker-output", choices=["discard", "inherit"], default="discard")
    return parser.parse_args()


def _matches_filter(spec: ScenarioSpec, raw_filter: str) -> bool:
    token = raw_filter.strip().lower()
    if token == "":
        return False
    if spec.scenario_id.lower() == token:
        return True
    if str(spec.stage_id) == token:
        return True
    if spec.title.lower().startswith(token):
        return True
    return False


def _select_scenarios(size_name: str, filters: list[str]) -> list[ScenarioDef]:
    start_stage, end_stage = SIZE_STAGE_RANGES[size_name]
    selected: list[ScenarioDef] = []
    for definition in _scenario_definitions():
        spec = definition.spec
        filter_match = any(_matches_filter(spec, user_filter) for user_filter in filters)
        in_size_window = start_stage <= spec.stage_id <= end_stage

        # Explicit --filter wins over profile window so single-scenario runs
        # can be executed with any size profile.
        if filters:
            if not filter_match:
                continue
        else:
            if not in_size_window:
                continue

        selected.append(definition)
    return selected


def _list_scenarios() -> None:
    print("ID    Stage  Title                          Workload     Limit")
    print("----  -----  -----------------------------  -----------  -----------------------------")
    for definition in _scenario_definitions():
        spec = definition.spec
        workload = "n/a"
        if spec.work_units is not None and spec.work_unit_label is not None:
            workload = f"{spec.work_units:.0f} {spec.work_unit_label}"
        print(
            f"{spec.scenario_id:<4}  {spec.stage_id:>5}  {spec.title:<29}  {workload:<11}  {_clip(spec.success_limit, 29)}"
        )


def _apply_integration_trace_environment(
    startup_options: StartupOptions,
    config: RunnerConfig,
) -> dict[str, str | None]:
    previous_values: dict[str, str | None] = {
        TRACE_LEVEL_ENV: os.environ.get(TRACE_LEVEL_ENV),
        TRACE_MODULES_ENV: os.environ.get(TRACE_MODULES_ENV),
        BROKER_OUTPUT_ENV: os.environ.get(BROKER_OUTPUT_ENV),
        TARGET_HOST_ENV: os.environ.get(TARGET_HOST_ENV),
        TARGET_PORT_ENV: os.environ.get(TARGET_PORT_ENV),
        TARGET_WS_PORT_ENV: os.environ.get(TARGET_WS_PORT_ENV),
    }

    if startup_options.trace_level is None:
        os.environ.pop(TRACE_LEVEL_ENV, None)
    else:
        os.environ[TRACE_LEVEL_ENV] = startup_options.trace_level

    if startup_options.trace_modules:
        os.environ[TRACE_MODULES_ENV] = ",".join(startup_options.trace_modules)
    else:
        os.environ.pop(TRACE_MODULES_ENV, None)

    os.environ[BROKER_OUTPUT_ENV] = "inherit" if startup_options.broker_output_inherit else "discard"
    os.environ[TARGET_HOST_ENV] = config.host
    os.environ[TARGET_PORT_ENV] = str(config.port)
    if config.ws_port is None:
        os.environ.pop(TARGET_WS_PORT_ENV, None)
    else:
        os.environ[TARGET_WS_PORT_ENV] = str(config.ws_port)

    return previous_values


def _restore_integration_trace_environment(previous_values: dict[str, str | None]) -> None:
    for name, value in previous_values.items():
        if value is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = value


def _run_scenarios(selected: list[ScenarioDef], repeat: int, config: RunnerConfig) -> list[ScenarioResult]:
    results: list[ScenarioResult] = []
    for definition in selected:
        spec = definition.spec
        print(f"\n[SCENARIO {spec.scenario_id}] {spec.title}")
        print(f"  purpose : {_clip(spec.purpose)}")
        print(f"  limit   : {_clip(spec.success_limit)}")
        for run_index in range(1, repeat + 1):
            started_at = _now_utc_iso()
            started_perf = time.perf_counter()
            status = "PASS"
            counters: dict[str, int] = {}
            summary = ""
            throughput: float | None = None
            throughput_basis_seconds: float | None = None
            try:
                outcome = definition.execute(config)
                summary = outcome.summary
                counters = dict(outcome.counters)
                if not outcome.success:
                    status = "DEVIATION"
                if outcome.throughput_per_second is not None:
                    throughput = float(outcome.throughput_per_second)
                    throughput_basis_seconds = outcome.throughput_basis_seconds
                elif outcome.work_units is not None:
                    elapsed = max(time.perf_counter() - started_perf, 1e-6)
                    throughput = outcome.work_units / elapsed
                    throughput_basis_seconds = elapsed
            except BaseException as error:  # pylint: disable=broad-except
                status = "ABORTED"
                summary = str(error)

            duration_seconds = time.perf_counter() - started_perf
            finished_at = _now_utc_iso()
            rate_text = "n/a"
            if throughput is not None and spec.work_unit_label is not None:
                if throughput_basis_seconds is not None:
                    rate_text = f"{throughput:.2f} {spec.work_unit_label}/s (basis={throughput_basis_seconds:.3f}s)"
                else:
                    rate_text = f"{throughput:.2f} {spec.work_unit_label}/s"

            print(f"  run {run_index:02d} : {status:<9} t={duration_seconds:7.3f}s rate={rate_text}")
            print(f"           summary: {_clip(summary)}")

            results.append(
                ScenarioResult(
                    stage_id=spec.stage_id,
                    scenario_id=spec.scenario_id,
                    run_index=run_index,
                    title=spec.title,
                    status=status,
                    duration_seconds=duration_seconds,
                    throughput_per_second=throughput,
                    summary=_compact(summary),
                    counters=counters,
                    started_at=started_at,
                    finished_at=finished_at,
                )
            )
    return results


def _build_preflight_abort_results(selected: list[ScenarioDef], repeat: int, reason: str) -> list[ScenarioResult]:
    results: list[ScenarioResult] = []
    compact_reason = _compact(reason)
    for definition in selected:
        spec = definition.spec
        print(f"\n[SCENARIO {spec.scenario_id}] {spec.title}")
        print(f"  purpose : {_clip(spec.purpose)}")
        print(f"  limit   : {_clip(spec.success_limit)}")
        for run_index in range(1, repeat + 1):
            now_iso = _now_utc_iso()
            print("  run %02d : ABORTED   t=  0.000s rate=n/a" % run_index)
            print(f"           summary: {_clip(compact_reason)}")
            results.append(
                ScenarioResult(
                    stage_id=spec.stage_id,
                    scenario_id=spec.scenario_id,
                    run_index=run_index,
                    title=spec.title,
                    status="ABORTED",
                    duration_seconds=0.0,
                    throughput_per_second=None,
                    summary=compact_reason,
                    counters={},
                    started_at=now_iso,
                    finished_at=now_iso,
                )
            )
    return results


def _print_header(args: argparse.Namespace, config: RunnerConfig, selected: list[ScenarioDef]) -> None:
    start_stage, end_stage = SIZE_STAGE_RANGES[args.size]
    planned_total_seconds = _planned_total_duration_seconds(selected, config, args.repeat)
    print("=== MQTT Performance Run ===")
    print(f"target   : {config.host}:{config.port}")
    print(f"profile  : {args.size} stages={start_stage}-{end_stage}")
    if planned_total_seconds is not None:
        print(f"duration : {planned_total_seconds:.3f}s")
    print(f"repeat   : {args.repeat}")
    print(f"scenarios: {len(selected)}")


def _save_results(
    result_path: Path,
    args: argparse.Namespace,
    config: RunnerConfig,
    selected: list[ScenarioDef],
    results: list[ScenarioResult],
    run_started_at: str,
    run_finished_at: str,
) -> None:
    result_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "run_started_at": run_started_at,
        "run_finished_at": run_finished_at,
        "arguments": {
            "host": args.host,
            "port": args.port,
            "ws_port": args.ws_port,
            "timeout": args.timeout,
            "duration": args.duration,
            "size": args.size,
            "repeat": args.repeat,
            "filter": args.filter,
        },
        "resolved_target": asdict(config),
        "selected_scenarios": [asdict(item.spec) for item in selected],
        "results": [asdict(item) for item in results],
    }
    result_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def _print_summary(results: list[ScenarioResult], results_path: Path) -> None:
    total = len(results)
    total_seconds = sum(item.duration_seconds for item in results)
    counts = Counter(item.status for item in results)
    pass_count = counts.get("PASS", 0)
    deviation_count = counts.get("DEVIATION", 0)
    aborted_count = counts.get("ABORTED", 0)
    run_rate = 0.0
    if total_seconds > 0.0:
        run_rate = total / total_seconds

    print("\n=== Summary ===")
    print("result    : %s" % ("PASS" if deviation_count == 0 and aborted_count == 0 else "FAIL"))
    print(f"runs      : total={total} pass={pass_count} deviation={deviation_count} aborted={aborted_count}")
    print(f"runtime_actual : {total_seconds:.3f}s")
    print(f"run_rate  : {run_rate:.2f} runs/s")

    print("\nrun details")
    print("id    run  status     time_s   throughput/s")
    print("----  ---  ---------  -------  ----------------")
    for item in results:
        throughput_text = "n/a"
        if item.throughput_per_second is not None:
            throughput_text = f"{item.throughput_per_second:9.2f}"
        print(
            f"{item.scenario_id:<4}  {item.run_index:>3}  {item.status:<9}  "
            f"{item.duration_seconds:>7.3f}  {throughput_text}"
        )

    print(f"\nresult_file: {results_path}")


def main() -> int:
    args = _parse_args()
    if args.repeat < 1:
        raise RuntimeError("--repeat must be >= 1")

    if args.list:
        _list_scenarios()
        return 0

    selected = _select_scenarios(args.size, args.filter)
    if not selected:
        print("No scenarios selected. Use --list.")
        return 1

    resolved_host, resolution_message = _resolve_runner_target_host(args.host, args.port)
    resolved_ws_port = args.ws_port
    if resolved_ws_port is None and not _is_local_host(args.host):
        resolved_ws_port = DEFAULT_REMOTE_WS_PORT

    config = RunnerConfig(
        host=resolved_host,
        port=args.port,
        ws_port=resolved_ws_port,
        timeout_seconds=args.timeout,
        size_profile=args.size,
        duration_seconds=args.duration,
    )
    startup_options = StartupOptions(
        trace_level=args.trace_level,
        trace_modules=tuple(args.trace_module),
        broker_output_inherit=(args.broker_output == "inherit"),
    )

    if resolution_message is not None:
        print(f"[setup] {resolution_message}")

    _print_header(args, config, selected)

    run_started_at = _now_utc_iso()
    preflight_ok, preflight_error = _preflight_target_connectivity(config)

    if not preflight_ok:
        assert preflight_error is not None
        print(f"\n[preflight] FAILED: {_clip(preflight_error)}")
        print("[preflight] execution skipped; all selected runs marked ABORTED")
        results = _build_preflight_abort_results(selected, args.repeat, preflight_error)
    else:
        print("\n[preflight] OK: target reachable")
        previous_environment = _apply_integration_trace_environment(startup_options, config)
        try:
            results = _run_scenarios(selected, args.repeat, config)
        finally:
            _restore_integration_trace_environment(previous_environment)

    run_finished_at = _now_utc_iso()
    results_path = Path(args.results_file).expanduser().resolve()

    _save_results(
        result_path=results_path,
        args=args,
        config=config,
        selected=selected,
        results=results,
        run_started_at=run_started_at,
        run_finished_at=run_finished_at,
    )
    _print_summary(results, results_path)

    has_failures = any(item.status != "PASS" for item in results)
    return 1 if has_failures else 0


# Scenario modules import from this module and require shared underscore-prefixed helpers.
__all__ = [name for name in globals() if not name.startswith("__")]


if __name__ == "__main__":
    sys.exit(main())
