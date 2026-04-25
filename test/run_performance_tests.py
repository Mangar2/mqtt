#!/usr/bin/env python3
from __future__ import annotations

"""Self-contained performance runner with native scenario implementations.

This file intentionally does not call old load scenario modules.
All scenario logic is defined directly here.
"""

import argparse
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


TEST_DIR = Path(__file__).parent.resolve()
DEFAULT_RESULTS_FILE = TEST_DIR / "performance_test_results.json"

TRACE_LEVEL_ENV = "MQTT_INTEGRATION_TRACE_LEVEL"
TRACE_MODULES_ENV = "MQTT_INTEGRATION_TRACE_MODULES"
BROKER_OUTPUT_ENV = "MQTT_INTEGRATION_BROKER_OUTPUT"
TARGET_HOST_ENV = "MQTT_INTEGRATION_TARGET_HOST"
TARGET_PORT_ENV = "MQTT_INTEGRATION_TARGET_PORT"
TARGET_WS_PORT_ENV = "MQTT_INTEGRATION_TARGET_WS_PORT"

DEFAULT_REMOTE_WS_PORT = 8083


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


def _scenario_s01(config: RunnerConfig) -> ScenarioOutcome:
    ranges = {
        "small": (10, 100, 10, 0.25, 0.01),
        "middle": (50, 1000, 50, 0.12, 0.003),
        "large": (500, 10000, 500, 0.05, 0.001),
    }
    min_connections, max_connections, step, gap_start, gap_end = ranges[config.size_profile]

    test_duration_seconds = 120.0
    hold_phase_seconds = 30.0
    started = time.monotonic()
    deadline = started + test_duration_seconds

    total_requested = 0
    total_success = 0
    total_rejected = 0
    total_network_close = 0
    total_local_resource = 0
    total_other = 0
    total_closed = 0
    total_wave_runtime = 0.0
    peak_open_connections = 0
    first_overload_target = 0
    first_overload_target_rate = 0.0
    first_overload_achieved_rate = 0.0
    s01_step_index = 1

    def _step(message: str) -> None:
        nonlocal s01_step_index
        print(f"  [S01 STEP {s01_step_index:02d}] {message}")
        s01_step_index += 1

    _step(
        f"profile={config.size_profile} range={min_connections}-{max_connections} "
        f"duration=120s hold=30s"
    )

    # Phase 1: open minimum load and hold for 30s.
    _step(f"phase-1 open baseline {min_connections} parallel connections")
    baseline_counters, baseline_sockets, baseline_duration, baseline_submit_duration = _run_connection_wave(
        config,
        requested_connections=min_connections,
        submit_gap_seconds=gap_start,
        connect_timeout_seconds=max(1.0, min(config.timeout_seconds, 3.0)),
        stop_deadline_monotonic=deadline,
    )
    total_requested += baseline_counters["requested"]
    total_success += baseline_counters["success"]
    total_rejected += baseline_counters["rejected"]
    total_network_close += baseline_counters["network_close"]
    total_local_resource += baseline_counters["local_resource"]
    total_other += baseline_counters["other"]
    total_wave_runtime += baseline_duration
    peak_open_connections = max(peak_open_connections, len(baseline_sockets))
    baseline_offered_rate = baseline_counters["requested"] / baseline_submit_duration
    baseline_accepted_rate = baseline_counters["success"] / baseline_submit_duration
    _step(
        f"phase-1 result success={baseline_counters['success']}/{baseline_counters['requested']} "
        f"offered={baseline_offered_rate:.1f}/s accepted={baseline_accepted_rate:.1f}/s"
    )

    hold_seconds = min(hold_phase_seconds, max(0.0, deadline - time.monotonic()))
    if hold_seconds > 0.0:
        _step(f"phase-1 hold {len(baseline_sockets)} open connections for {hold_seconds:.1f}s")
        time.sleep(hold_seconds)
    total_closed += _close_sockets(baseline_sockets)
    _step(f"phase-1 cleanup closed={total_closed}")

    # Phase 2: progressively increase requested connections and accelerate open rate.
    _step("phase-2 start accelerated ramp")
    ramp_targets = list(range(min_connections + step, max_connections + 1, step))
    if not ramp_targets:
        ramp_targets = [max_connections]

    ramp_window_seconds = max(0.0, test_duration_seconds - hold_phase_seconds)
    _step(f"phase-2 plan waves={len(ramp_targets)} ramp_window={ramp_window_seconds:.1f}s")

    max_target_reached = False
    highest_target_reached = min_connections

    wave_count = len(ramp_targets)
    checkpoint_indices = {
        1,
        max(1, math.ceil(wave_count * 0.25)),
        max(1, math.ceil(wave_count * 0.50)),
        max(1, math.ceil(wave_count * 0.75)),
        wave_count,
    }

    for wave_index, requested_for_round in enumerate(ramp_targets, start=1):
        if time.monotonic() >= deadline:
            break

        remaining_targets = max(1, wave_count - wave_index + 1)
        remaining_time = max(0.0, deadline - time.monotonic())
        slot_seconds = remaining_time / float(remaining_targets)

        # Keep submit and settle phases coupled to the current target and available time.
        submit_budget_seconds = max(0.4, slot_seconds * 0.7)
        submit_gap = max(gap_end, min(gap_start, submit_budget_seconds / float(requested_for_round)))
        target_rate = 1.0 / submit_gap if submit_gap > 0.0 else 0.0

        counters, opened_sockets, wave_duration, submit_duration = _run_connection_wave(
            config,
            requested_connections=requested_for_round,
            submit_gap_seconds=submit_gap,
            connect_timeout_seconds=max(0.8, min(config.timeout_seconds, 2.5)),
            stop_deadline_monotonic=deadline,
        )
        total_requested += counters["requested"]
        total_success += counters["success"]
        total_rejected += counters["rejected"]
        total_network_close += counters["network_close"]
        total_local_resource += counters["local_resource"]
        total_other += counters["other"]
        total_wave_runtime += wave_duration
        peak_open_connections = max(peak_open_connections, len(opened_sockets))
        highest_target_reached = max(highest_target_reached, requested_for_round)
        if requested_for_round >= max_connections:
            max_target_reached = True

        accepted_rate = (counters["success"] / submit_duration) if submit_duration > 0.0 else 0.0
        completion_rate = (counters["success"] / wave_duration) if wave_duration > 0.0 else 0.0
        overloaded = accepted_rate < (target_rate * 0.85)
        if overloaded and first_overload_target == 0:
            first_overload_target = requested_for_round
            first_overload_target_rate = target_rate
            first_overload_achieved_rate = accepted_rate
            _step(
                f"overload detected conn_target={requested_for_round} "
                f"rate_target={target_rate:.1f}/s rate_accepted={accepted_rate:.1f}/s "
                f"threshold={(target_rate * 0.85):.1f}/s"
            )

        if wave_index in checkpoint_indices:
            progress_percent = int((wave_index / float(wave_count)) * 100.0)
            _step(
                f"ramp {progress_percent}% conn_target={requested_for_round} "
                f"success={counters['success']}/{counters['requested']} "
                f"rate_target={target_rate:.1f}/s rate_accepted={accepted_rate:.1f}/s "
                f"rate_complete={completion_rate:.1f}/s"
            )

        remaining_after_wave = max(0.0, deadline - time.monotonic())
        hold_this_round = max(0.0, min(slot_seconds * 0.3, remaining_after_wave))
        if hold_this_round > 0.0:
            time.sleep(hold_this_round)
        total_closed += _close_sockets(opened_sockets)

    _step("phase-2 finished at deadline")

    avg_connect_rate = (total_success / total_wave_runtime) if total_wave_runtime > 0.0 else 0.0
    counters = {
        "range_min": min_connections,
        "range_max": max_connections,
        "requested": total_requested,
        "success": total_success,
        "rejected": total_rejected,
        "network_close": total_network_close,
        "local_resource": total_local_resource,
        "other": total_other,
        "peak_open": peak_open_connections,
        "closed": total_closed,
        "overload_target": first_overload_target,
    }

    overload_text = "none"
    if first_overload_target > 0:
        overload_text = (
            f"target={first_overload_target} requested_rate={first_overload_target_rate:.1f}/s "
            f"achieved_rate={first_overload_achieved_rate:.1f}/s"
        )

    summary = (
        f"profile={config.size_profile} range={min_connections}-{max_connections} duration=120s "
        f"requested={total_requested} success={total_success} fail={total_requested - total_success} "
        f"avg_connect_rate={avg_connect_rate:.2f}/s peak_open={peak_open_connections} "
        f"max_target_reached={max_target_reached} highest_target={highest_target_reached} "
        f"overload={overload_text}"
    )
    _step(
        f"final requested={total_requested} success={total_success} "
        f"avg_rate={avg_connect_rate:.2f}/s peak_open={peak_open_connections} "
        f"highest_target={highest_target_reached}"
    )
    success = total_success > 0 and total_closed == total_success and max_target_reached
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(total_success))


def _connect_client(config: RunnerConfig, prefix: str, timeout: float | None = None, clean_start: bool = True, client_id: str = "", properties=None):
    client = MqttClient(timeout_seconds=timeout or config.timeout_seconds)
    result = client.connect(
        config.host,
        config.port,
        client_id=client_id or f"{prefix}-{_unique_suffix()}",
        clean_start=clean_start,
        properties=properties,
    )
    if int(getattr(result, "reason_code", 255)) != 0:
        raise RuntimeError(f"connect failed with reason={int(getattr(result, 'reason_code', 255))}")
    return client


def _scenario_p01(config: RunnerConfig) -> ScenarioOutcome:
    profile_rate_ranges: dict[str, tuple[float, float]] = {
        "small": (100.0, 2000.0),
        "middle": (1000.0, 20000.0),
        "large": (10000.0, 100000.0),
    }
    ramp_start_rate, ramp_end_rate = profile_rate_ranges[config.size_profile]
    ramp_duration_seconds = 90.0
    hold_duration_seconds = 30.0
    duration_seconds = ramp_duration_seconds + hold_duration_seconds
    rate_step_seconds = 10.0
    ramp_step_count = int(ramp_duration_seconds / rate_step_seconds)
    rate_step_per_level = (ramp_end_rate - ramp_start_rate) / float(ramp_step_count)
    overload_deficit_threshold = 0.30
    report_interval_seconds = 10.0
    topic = f"perf/p01/{_unique_suffix()}"
    step = _scenario_step_logger("P01")

    def _mmss(seconds_value: float) -> str:
        total_seconds = max(0, int(round(seconds_value)))
        minutes = total_seconds // 60
        seconds = total_seconds % 60
        return f"{minutes}:{seconds:02d}"

    step(
        f"duration={duration_seconds:.0f}s ramp={ramp_start_rate:.0f}->{ramp_end_rate:.0f}/s "
        f"ramp_step={rate_step_seconds:.0f}s hold={hold_duration_seconds:.0f}s report={report_interval_seconds:.0f}s"
    )

    sent_total = 0
    received_total = 0
    ramp_stopped = False
    ramp_stopped_at_rate = 0.0

    with _connect_client(config, "p01-sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
        subscriber.subscribe(topic, qos=0)
        with _connect_client(config, "p01-pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            started = time.monotonic()
            ended = started + duration_seconds
            block_start = started
            block_end = min(ended, block_start + report_interval_seconds)
            block_sent = 0
            block_received = 0
            current_rate = ramp_start_rate
            current_interval = 1.0 / current_rate
            sequence = 1

            while time.monotonic() < ended:
                now = time.monotonic()
                block_elapsed = max(0.0, min(now, block_end) - block_start)
                required_sent_until_now = int(math.floor((block_elapsed / current_interval) + 1e-9))
                messages_to_send_now = max(0, required_sent_until_now - block_sent)

                for _ in range(messages_to_send_now):
                    publisher.publish(
                        topic,
                        f"p01:{sequence}".encode("utf-8"),
                        qos=0,
                        wait_for_qos0_publish=False,
                    )
                    sent_total += 1
                    block_sent += 1
                    sequence += 1

                drained = subscriber.drain_available_messages(limit=4096)
                if drained:
                    received_count = len(drained)
                    received_total += received_count
                    block_received += received_count

                now = time.monotonic()
                while now >= block_end and block_start < ended:
                    interval_elapsed = max(1e-6, block_end - block_start)
                    send_rate = _safe_rate(float(block_sent), interval_elapsed)
                    receive_rate = _safe_rate(float(block_received), interval_elapsed)
                    stamp = _mmss(block_end - started)
                    step(
                        f"{stamp:>5} send: {block_sent:>5d} {send_rate:>6.1f}/s "
                        f"recv: {block_received:>5d} {receive_rate:>6.1f}/s"
                    )

                    if (not ramp_stopped) and block_end <= (started + ramp_duration_seconds):
                        deficit_ratio = (
                            (float(block_sent - block_received) / float(block_sent))
                            if block_sent > 0
                            else 0.0
                        )
                        if deficit_ratio >= overload_deficit_threshold:
                            ramp_stopped = True
                            ramp_stopped_at_rate = current_rate
                        elif current_rate < ramp_end_rate:
                            current_rate = min(ramp_end_rate, current_rate + rate_step_per_level)
                            current_interval = 1.0 / current_rate

                    block_sent = 0
                    block_received = 0
                    block_start = block_end
                    block_end = min(ended, block_start + report_interval_seconds)
                    now = time.monotonic()

                time.sleep(0.001)

            flush_deadline = time.monotonic() + 1.0
            while time.monotonic() < flush_deadline:
                drained = subscriber.drain_available_messages(limit=4096)
                if not drained:
                    time.sleep(0.005)
                    continue
                received_total += len(drained)

            elapsed = max(1e-6, time.monotonic() - started)

    planned_sent = int(
        sum(
            (
                ramp_start_rate + (rate_step_per_level * float(level_index))
            )
            * rate_step_seconds
            for level_index in range(ramp_step_count)
        )
        + (ramp_end_rate * hold_duration_seconds)
    )
    send_rate_total = _safe_rate(float(sent_total), elapsed)
    recv_rate_total = _safe_rate(float(received_total), elapsed)
    delivery_ratio_total = (float(received_total) / float(sent_total)) if sent_total > 0 else 0.0

    counters = {
        "planned_sent": planned_sent,
        "sent": sent_total,
        "received": received_total,
        "missing": max(0, sent_total - received_total),
        "ramp_stopped": int(ramp_stopped),
        "ramp_stopped_at_rate": ramp_stopped_at_rate,
    }
    summary = (
        f"duration={elapsed:.3f}s planned_sent={planned_sent} sent={sent_total} received={received_total} "
        f"send_rate={send_rate_total:.2f}/s recv_rate={recv_rate_total:.2f}/s delivery_ratio={delivery_ratio_total:.4f} "
        f"ramp_stopped={ramp_stopped}"
    )
    final_stamp = _mmss(elapsed)
    step(
        f"{final_stamp:>5} send: {sent_total:>5d} {send_rate_total:>6.1f}/s "
        f"recv: {received_total:>5d} {recv_rate_total:>6.1f}/s"
    )
    return ScenarioOutcome(
        success=sent_total > 0,
        summary=summary,
        counters=counters,
        work_units=float(received_total),
        throughput_per_second=recv_rate_total,
        throughput_basis_seconds=elapsed,
    )


def _scenario_p02(config: RunnerConfig) -> ScenarioOutcome:
    profile_rate_ranges: dict[str, tuple[float, float]] = {
        "small": (10.0, 2000.0),
        "middle": (100.0, 20000.0),
        "large": (1000.0, 100000.0),
    }
    ramp_start_rate, ramp_end_rate = profile_rate_ranges[config.size_profile]
    ramp_duration_seconds = 90.0
    hold_duration_seconds = 30.0
    duration_seconds = ramp_duration_seconds + hold_duration_seconds
    rate_step_seconds = 10.0
    ramp_step_count = int(ramp_duration_seconds / rate_step_seconds)
    rate_step_per_level = (ramp_end_rate - ramp_start_rate) / float(ramp_step_count)
    overload_deficit_threshold = 0.30
    report_interval_seconds = 10.0
    topic = f"perf/p02/{_unique_suffix()}"
    step = _scenario_step_logger("P02")

    def _mmss(seconds_value: float) -> str:
        total_seconds = max(0, int(round(seconds_value)))
        minutes = total_seconds // 60
        seconds = total_seconds % 60
        return f"{minutes}:{seconds:02d}"

    step(
        f"duration={duration_seconds:.0f}s ramp={ramp_start_rate:.0f}->{ramp_end_rate:.0f}/s "
        f"ramp_step={rate_step_seconds:.0f}s hold={hold_duration_seconds:.0f}s report={report_interval_seconds:.0f}s"
    )

    sent_total = 0
    ack_total = 0
    received_total = 0
    ramp_stopped = False
    ramp_stopped_at_rate = 0.0
    pending_publish_mids: set[int] = set()

    with _connect_client(config, "p02-sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
        subscriber.subscribe(topic, qos=1)
        with _connect_client(config, "p02-pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            started = time.monotonic()
            ended = started + duration_seconds
            block_start = started
            block_end = min(ended, block_start + report_interval_seconds)
            block_sent = 0
            block_received = 0
            current_rate = ramp_start_rate
            current_interval = 1.0 / current_rate
            sequence = 1

            while time.monotonic() < ended:
                now = time.monotonic()
                block_elapsed = max(0.0, min(now, block_end) - block_start)
                required_sent_until_now = int(math.floor((block_elapsed / current_interval) + 1e-9))
                messages_to_send_now = max(0, required_sent_until_now - block_sent)

                for _ in range(messages_to_send_now):
                    publish_mid = int(
                        publisher.publish(
                            topic,
                            f"p02:{sequence}".encode("utf-8"),
                            qos=1,
                            wait_for_qos1_publish=False,
                        )
                    )
                    pending_publish_mids.add(publish_mid)
                    sent_total += 1
                    block_sent += 1
                    sequence += 1

                completed_mids = publisher.drain_published_mids(limit=65536)
                if completed_mids:
                    for mid_value, reason_value in completed_mids.items():
                        pending_publish_mids.discard(mid_value)
                        if reason_value in (0x00, 0x10):
                            ack_total += 1

                drained = subscriber.drain_available_messages(limit=4096)
                if drained:
                    received_count = len(drained)
                    received_total += received_count
                    block_received += received_count

                now = time.monotonic()
                while now >= block_end and block_start < ended:
                    interval_elapsed = max(1e-6, block_end - block_start)
                    send_rate = _safe_rate(float(block_sent), interval_elapsed)
                    receive_rate = _safe_rate(float(block_received), interval_elapsed)
                    stamp = _mmss(block_end - started)
                    step(
                        f"{stamp:>5} send: {block_sent:>5d} {send_rate:>6.1f}/s "
                        f"recv: {block_received:>5d} {receive_rate:>6.1f}/s"
                    )

                    if (not ramp_stopped) and block_end <= (started + ramp_duration_seconds):
                        deficit_ratio = (
                            (float(block_sent - block_received) / float(block_sent))
                            if block_sent > 0
                            else 0.0
                        )
                        if deficit_ratio >= overload_deficit_threshold:
                            ramp_stopped = True
                            ramp_stopped_at_rate = current_rate
                        elif current_rate < ramp_end_rate:
                            current_rate = min(ramp_end_rate, current_rate + rate_step_per_level)
                            current_interval = 1.0 / current_rate

                    block_sent = 0
                    block_received = 0
                    block_start = block_end
                    block_end = min(ended, block_start + report_interval_seconds)
                    now = time.monotonic()

                time.sleep(0.001)

            flush_deadline = time.monotonic() + 1.0
            while time.monotonic() < flush_deadline:
                completed_mids = publisher.drain_published_mids(limit=65536)
                if completed_mids:
                    for mid_value, reason_value in completed_mids.items():
                        pending_publish_mids.discard(mid_value)
                        if reason_value in (0x00, 0x10):
                            ack_total += 1

                drained = subscriber.drain_available_messages(limit=4096)
                if not drained:
                    time.sleep(0.005)
                    continue
                received_total += len(drained)

            elapsed = max(1e-6, time.monotonic() - started)

    planned_sent = int(
        sum(
            (
                ramp_start_rate + (rate_step_per_level * float(level_index))
            )
            * rate_step_seconds
            for level_index in range(ramp_step_count)
        )
        + (ramp_end_rate * hold_duration_seconds)
    )
    send_rate_total = _safe_rate(float(sent_total), elapsed)
    recv_rate_total = _safe_rate(float(received_total), elapsed)
    ack_rate_total = _safe_rate(float(ack_total), elapsed)
    delivery_ratio_total = (float(received_total) / float(sent_total)) if sent_total > 0 else 0.0
    ack_ratio_total = (float(ack_total) / float(sent_total)) if sent_total > 0 else 0.0

    success = delivery_ratio_total >= 0.99 and ack_ratio_total >= 0.99
    counters = {
        "planned_sent": planned_sent,
        "sent": sent_total,
        "acked": ack_total,
        "received": received_total,
        "missing": max(0, sent_total - received_total),
        "unacked": max(0, sent_total - ack_total),
        "pending_publish_mids": len(pending_publish_mids),
        "ramp_stopped": int(ramp_stopped),
        "ramp_stopped_at_rate": ramp_stopped_at_rate,
    }
    summary = (
        f"duration={elapsed:.3f}s planned_sent={planned_sent} sent={sent_total} acked={ack_total} received={received_total} "
        f"send_rate={send_rate_total:.2f}/s ack_rate={ack_rate_total:.2f}/s recv_rate={recv_rate_total:.2f}/s "
        f"ack_ratio={ack_ratio_total:.4f} delivery_ratio={delivery_ratio_total:.4f} ramp_stopped={ramp_stopped}"
    )
    final_stamp = _mmss(elapsed)
    step(
        f"{final_stamp:>5} send: {sent_total:>5d} {send_rate_total:>6.1f}/s "
        f"recv: {received_total:>5d} {recv_rate_total:>6.1f}/s"
    )
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(received_total),
        throughput_per_second=recv_rate_total,
        throughput_basis_seconds=elapsed,
    )


def _scenario_p03(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P03[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p03/{_unique_suffix()}"
    step = _scenario_step_logger("P03")
    ramp_rates = list(_ramp_steps(parameters["start_msg_s"], parameters["end_msg_s"], parameters["step"]))
    ramp_checkpoints = _progress_checkpoints(len(ramp_rates))

    step(
        f"profile={config.size_profile} ramp={parameters['start_msg_s']}-{parameters['end_msg_s']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_rates)} topic={topic}")

    hold_sent = 0
    hold_completed = 0
    hold_delivered = 0
    peak_pending = 0

    with _connect_client(config, "p03-sub", timeout=max(config.timeout_seconds, 25.0)) as subscriber:
        subscriber.subscribe(topic, qos=2)
        with _connect_client(config, "p03-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
            for rate_index, rate in enumerate(ramp_rates, start=1):
                sent_ramp = 0
                completed_ramp = 0
                level_started = time.monotonic()
                for index in range(rate):
                    sent_ramp += 1
                    reason = int(publisher.publish(topic, f"p03-r-{rate}-{index}".encode("utf-8"), qos=2))
                    if reason in (0x00, 0x10):
                        completed_ramp += 1
                peak_pending = max(peak_pending, sent_ramp - completed_ramp)
                delivered = _drain_until(subscriber, rate, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if rate_index in ramp_checkpoints:
                    progress = int((rate_index / float(len(ramp_rates))) * 100.0)
                    level_elapsed = max(1e-6, time.monotonic() - level_started)
                    actual_rate = float(delivered) / level_elapsed
                    step(
                        f"ramp {progress}% target_msgs_per_step={rate} completed={completed_ramp}/{sent_ramp} "
                        f"delivered={delivered}/{rate} step_elapsed={level_elapsed:.3f}s "
                        f"actual_msgs_per_s={actual_rate:.2f}"
                    )
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            hold_rate = int(parameters["end_msg_s"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} target_msgs_per_round={hold_rate}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                sent_round = 0
                completed_round = 0
                for item_index in range(hold_rate):
                    hold_sent += 1
                    sent_round += 1
                    reason = int(publisher.publish(topic, f"p03-h-{round_index}-{item_index}".encode("utf-8"), qos=2))
                    if reason in (0x00, 0x10):
                        hold_completed += 1
                        completed_round += 1
                peak_pending = max(peak_pending, sent_round - completed_round)
                delivered = _drain_until(subscriber, hold_rate, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                hold_delivered += delivered
                if (round_index + 1) in hold_checkpoints:
                    ratio_complete = (hold_completed / hold_sent) if hold_sent > 0 else 0.0
                    ratio_delivered = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
                    hold_elapsed_now = max(1e-6, time.monotonic() - hold_started)
                    actual_rate_now = float(hold_delivered) / hold_elapsed_now
                    step(
                        f"hold {(round_index + 1)}/{hold_rounds} completed={hold_completed}/{hold_sent} "
                        f"delivered={hold_delivered}/{hold_sent} pending_peak={peak_pending}"
                    )
                    step(
                        f"hold ratios complete={ratio_complete:.4f} delivery={ratio_delivered:.4f} "
                        f"hold_elapsed={hold_elapsed_now:.3f}s actual_msgs_per_s={actual_rate_now:.2f}"
                    )
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)

    ratio_complete = (hold_completed / hold_sent) if hold_sent > 0 else 0.0
    ratio_delivered = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
    success = ratio_complete >= 0.99 and ratio_delivered >= 0.99
    counters = {
        "hold_sent": hold_sent,
        "hold_completed": hold_completed,
        "hold_delivered": hold_delivered,
        "peak_pending": peak_pending,
    }
    summary = (
        f"hold_sent={hold_sent} complete_ratio={ratio_complete:.4f} "
        f"deliver_ratio={ratio_delivered:.4f} peak_pending={peak_pending}"
    )
    step(f"final success={success} complete_ratio={ratio_complete:.4f} delivery_ratio={ratio_delivered:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p04(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P04[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p04/{_unique_suffix()}"
    step = _scenario_step_logger("P04")
    ramp_targets = list(
        _ramp_steps(
            int(parameters["start_subscribers"]),
            int(parameters["end_subscribers"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_targets))

    step(
        f"profile={config.size_profile} subscribers={parameters['start_subscribers']}-{parameters['end_subscribers']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_targets)} msg_per_step={parameters['msg_per_step']}")

    subscribers: list[MqttClient] = []
    hold_expected = 0
    hold_delivered = 0
    try:
        with _connect_client(config, "p04-pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            subscriber_target = int(parameters["start_subscribers"])
            while len(subscribers) < subscriber_target:
                subscriber = _connect_client(config, f"p04-sub{len(subscribers)}", timeout=max(config.timeout_seconds, 20.0))
                subscriber.subscribe(topic, qos=int(parameters["qos"]))
                subscribers.append(subscriber)

            for level_index, subscriber_target in enumerate(ramp_targets, start=1):
                while len(subscribers) < subscriber_target:
                    subscriber = _connect_client(config, f"p04-sub{len(subscribers)}", timeout=max(config.timeout_seconds, 20.0))
                    subscriber.subscribe(topic, qos=int(parameters["qos"]))
                    subscribers.append(subscriber)

                for message_index in range(int(parameters["msg_per_step"])):
                    publisher.publish(topic, f"p04-r-{subscriber_target}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                for subscriber in subscribers:
                    _drain_until(subscriber, int(parameters["msg_per_step"]), timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_targets))) * 100.0)
                    step(f"ramp {progress}% subscribers={len(subscribers)}")
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            hold_messages = int(parameters["msg_per_step"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} subscribers={len(subscribers)}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                for message_index in range(hold_messages):
                    publisher.publish(topic, f"p04-h-{round_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                hold_expected += hold_messages * len(subscribers)
                for subscriber in subscribers:
                    hold_delivered += _drain_until(subscriber, hold_messages, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                if (round_index + 1) in hold_checkpoints:
                    ratio = (hold_delivered / hold_expected) if hold_expected > 0 else 0.0
                    step(f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_expected} ratio={ratio:.4f}")
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for subscriber in subscribers:
            try:
                subscriber.disconnect()
            except BaseException:
                pass

    ratio = (hold_delivered / hold_expected) if hold_expected > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_expected": hold_expected,
        "hold_delivered": hold_delivered,
        "subscribers_peak": len(subscribers),
    }
    summary = f"hold_expected={hold_expected} hold_delivered={hold_delivered} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f} peak_subscribers={len(subscribers)}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p05(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P05[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p05/{_unique_suffix()}"
    step = _scenario_step_logger("P05")
    ramp_targets = list(
        _ramp_steps(
            int(parameters["start_publishers"]),
            int(parameters["end_publishers"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_targets))

    step(
        f"profile={config.size_profile} publishers={parameters['start_publishers']}-{parameters['end_publishers']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_targets)} msg_per_pub={parameters['msg_per_publisher_per_step']}")

    publishers: list[MqttClient] = []
    hold_sent = 0
    hold_delivered = 0
    try:
        with _connect_client(config, "p05-sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
            subscriber.subscribe(topic, qos=int(parameters["qos"]))

            while len(publishers) < int(parameters["start_publishers"]):
                publishers.append(_connect_client(config, f"p05-pub{len(publishers)}", timeout=max(config.timeout_seconds, 20.0)))

            for level_index, publisher_target in enumerate(ramp_targets, start=1):
                while len(publishers) < publisher_target:
                    publishers.append(_connect_client(config, f"p05-pub{len(publishers)}", timeout=max(config.timeout_seconds, 20.0)))

                messages_per_publisher = int(parameters["msg_per_publisher_per_step"])
                for publisher_index, publisher in enumerate(publishers):
                    for message_index in range(messages_per_publisher):
                        publisher.publish(topic, f"p05-r-{publisher_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                expected_step = len(publishers) * messages_per_publisher
                _drain_until(subscriber, expected_step, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_targets))) * 100.0)
                    step(f"ramp {progress}% publishers={len(publishers)} expected_step={expected_step}")
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            messages_per_publisher = int(parameters["msg_per_publisher_per_step"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} publishers={len(publishers)}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                for publisher_index, publisher in enumerate(publishers):
                    for message_index in range(messages_per_publisher):
                        publisher.publish(topic, f"p05-h-{round_index}-{publisher_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                expected_round = len(publishers) * messages_per_publisher
                hold_sent += expected_round
                hold_delivered += _drain_until(subscriber, expected_round, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                if (round_index + 1) in hold_checkpoints:
                    ratio = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
                    step(f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_sent} ratio={ratio:.4f}")
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for publisher in publishers:
            try:
                publisher.disconnect()
            except BaseException:
                pass

    ratio = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_sent": hold_sent,
        "hold_delivered": hold_delivered,
        "publishers_peak": len(publishers),
    }
    summary = f"hold_sent={hold_sent} hold_delivered={hold_delivered} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f} peak_publishers={len(publishers)}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p06(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P06[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p06/{_unique_suffix()}"
    step = _scenario_step_logger("P06")
    payload_levels = list(
        _ramp_steps(
            int(parameters["start_payload_bytes"]),
            int(parameters["end_payload_bytes"]),
            int(parameters["step_bytes"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(payload_levels))

    step(
        f"profile={config.size_profile} payload={parameters['start_payload_bytes']}-{parameters['end_payload_bytes']} "
        f"step={parameters['step_bytes']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(payload_levels)} burst={parameters['burst_count']}")

    qos = int(parameters["qos"])
    with _connect_client(config, "p06-sub", timeout=max(config.timeout_seconds, 30.0)) as subscriber:
        subscriber.subscribe(topic, qos=qos)
        with _connect_client(config, "p06-pub", timeout=max(config.timeout_seconds, 30.0)) as publisher:
            for level_index, payload_size in enumerate(payload_levels, start=1):
                payload = (b"r" * payload_size)
                publisher.publish(topic, payload, qos=qos)
                _drain_until(subscriber, 1, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(payload_levels))) * 100.0)
                    step(f"ramp {progress}% payload_bytes={payload_size}")
                time.sleep(parameters["ramp_interval_s"])

            burst_count = int(parameters["burst_count"])
            peak_payload = b"h" * int(parameters["end_payload_bytes"])
            step(f"hold burst start count={burst_count} payload_bytes={parameters['end_payload_bytes']}")
            hold_start = time.monotonic()
            for _index in range(burst_count):
                publisher.publish(topic, peak_payload, qos=qos)
            hold_delivered = _drain_until(subscriber, burst_count, timeout_seconds=max(hold_seconds, 2.0))
            hold_elapsed = max(1e-6, time.monotonic() - hold_start)

    delivered_bytes = hold_delivered * int(parameters["end_payload_bytes"])
    throughput_bytes_s = delivered_bytes / hold_elapsed
    success = hold_delivered == int(parameters["burst_count"])
    counters = {
        "hold_burst": int(parameters["burst_count"]),
        "hold_delivered": hold_delivered,
        "payload_bytes": int(parameters["end_payload_bytes"]),
        "delivered_bytes": delivered_bytes,
    }
    summary = (
        f"burst={parameters['burst_count']} delivered={hold_delivered} payload={parameters['end_payload_bytes']} "
        f"bytes throughput={throughput_bytes_s:.2f}"
    )
    step(f"final success={success} delivered={hold_delivered}/{burst_count} throughput={throughput_bytes_s:.2f}B/s")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(delivered_bytes),
        throughput_per_second=_safe_rate(float(delivered_bytes), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p07(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P07[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p07/{_unique_suffix()}"
    qos = int(parameters["qos"])
    step = _scenario_step_logger("P07")
    ramp_topics = list(
        _ramp_steps(
            int(parameters["start_topics"]),
            int(parameters["end_topics"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_topics))

    step(
        f"profile={config.size_profile} topics={parameters['start_topics']}-{parameters['end_topics']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_topics)} root={root}")

    created_topics: list[str] = []
    replay_attempts = 0
    replay_ok = 0
    hold_replayed = 0
    hold_expected = 0

    try:
        with _connect_client(config, "p07-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
            last_count = 0
            for level_index, topic_count in enumerate(ramp_topics, start=1):
                for index in range(last_count, topic_count):
                    topic = f"{root}/{index}"
                    created_topics.append(topic)
                    publisher.publish(topic, f"ret-{index}".encode("utf-8"), qos=qos, retain=True)
                last_count = topic_count

                replay_attempts += 1
                with _connect_client(config, "p07-ramp-sub", timeout=max(config.timeout_seconds, 25.0)) as ramp_subscriber:
                    ramp_subscriber.subscribe(f"{root}/#", qos=qos)
                    received = _drain_until(ramp_subscriber, topic_count, timeout_seconds=max(3.0, parameters["ramp_interval_s"] * 2.0))
                    if topic_count > 0 and (received / topic_count) >= 0.99:
                        replay_ok += 1
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_topics))) * 100.0)
                    step(f"ramp {progress}% topics={topic_count} replay_attempts={replay_attempts}")
                time.sleep(parameters["ramp_interval_s"])

            hold_deadline = time.monotonic() + max(1.0, hold_seconds)
            step(f"hold start target_topics={parameters['end_topics']}")
            hold_started = time.monotonic()
            while time.monotonic() < hold_deadline:
                replay_attempts += 1
                hold_expected += int(parameters["end_topics"])
                with _connect_client(config, "p07-hold-sub", timeout=max(config.timeout_seconds, 25.0)) as hold_subscriber:
                    hold_subscriber.subscribe(f"{root}/#", qos=qos)
                    received = _drain_until(hold_subscriber, int(parameters["end_topics"]), timeout_seconds=max(1.5, hold_seconds / 2.0))
                    hold_replayed += received
                    if int(parameters["end_topics"]) > 0 and (received / int(parameters["end_topics"])) >= 0.99:
                        replay_ok += 1
                hold_ratio = (hold_replayed / hold_expected) if hold_expected > 0 else 0.0
                step(
                    f"hold attempt={replay_attempts} replay_ok={replay_ok} "
                    f"replayed={hold_replayed}/{hold_expected} ratio={hold_ratio:.4f}"
                )
                if hold_seconds <= 1.0:
                    break
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        with _connect_client(config, "p07-clean", timeout=max(config.timeout_seconds, 25.0)) as cleanup_publisher:
            for topic in created_topics:
                cleanup_publisher.publish(topic, b"", qos=qos, retain=True)

    hold_ratio = (hold_replayed / hold_expected) if hold_expected > 0 else 0.0
    success = replay_attempts > 0 and replay_ok == replay_attempts and hold_ratio >= 0.99
    counters = {
        "replay_attempts": replay_attempts,
        "replay_ok": replay_ok,
        "hold_expected": hold_expected,
        "hold_replayed": hold_replayed,
    }
    summary = f"attempts={replay_attempts} replay_ok={replay_ok} hold_ratio={hold_ratio:.4f}"
    step(f"final success={success} attempts={replay_attempts} hold_ratio={hold_ratio:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_replayed),
        throughput_per_second=_safe_rate(float(hold_replayed), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p08(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P08[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p08/{_unique_suffix()}"
    client_id = f"p08-sub-{_unique_suffix()}"
    step = _scenario_step_logger("P08")
    queue_levels = list(
        _ramp_steps(
            int(parameters["start_queued"]),
            int(parameters["end_queued"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(queue_levels))

    step(
        f"profile={config.size_profile} queued={parameters['start_queued']}-{parameters['end_queued']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(queue_levels)} drain_timeout={parameters['drain_timeout_s']}s")

    connect_props = Properties(PacketTypes.CONNECT)
    setattr(connect_props, "SessionExpiryInterval", int(parameters["session_expiry_s"]))

    with _connect_client(config, "p08-init", clean_start=True, client_id=client_id, properties=connect_props) as initial_subscriber:
        initial_subscriber.subscribe(topic, qos=1)

    missing_total = 0
    hold_missing = 0
    hold_expected = 0
    hold_delivered = 0

    with _connect_client(config, "p08-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
        for level_index, queued_count in enumerate(queue_levels, start=1):
            for index in range(queued_count):
                publisher.publish(topic, f"p08-r-{queued_count}-{index}".encode("utf-8"), qos=1)

            with _connect_client(config, "p08-resume", clean_start=False, client_id=client_id, properties=connect_props) as resumed:
                drained = _drain_until(resumed, queued_count, timeout_seconds=float(parameters["drain_timeout_s"]))
            missing_total += max(0, queued_count - drained)
            if level_index in ramp_checkpoints:
                progress = int((level_index / float(len(queue_levels))) * 100.0)
                step(f"ramp {progress}% queued={queued_count} drained={drained}/{queued_count}")
            time.sleep(parameters["ramp_interval_s"])

        hold_rounds = max(1, int(math.ceil(hold_seconds / max(1.0, parameters["ramp_interval_s"]))))
        hold_checkpoints = _progress_checkpoints(hold_rounds)
        step(f"hold start rounds={hold_rounds} queued={parameters['end_queued']}")
        hold_started = time.monotonic()
        for round_index in range(hold_rounds):
            queued_count = int(parameters["end_queued"])
            for index in range(queued_count):
                publisher.publish(topic, f"p08-h-{round_index}-{index}".encode("utf-8"), qos=1)

            with _connect_client(config, "p08-hold", clean_start=False, client_id=client_id, properties=connect_props) as resumed:
                drained = _drain_until(resumed, queued_count, timeout_seconds=float(parameters["drain_timeout_s"]))
            hold_expected += queued_count
            hold_delivered += drained
            hold_missing += max(0, queued_count - drained)
            if (round_index + 1) in hold_checkpoints:
                step(
                    f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_expected} "
                    f"missing={hold_missing}"
                )
        hold_elapsed = max(1e-6, time.monotonic() - hold_started)

    success = hold_missing == 0 and missing_total == 0
    counters = {
        "missing_total": missing_total,
        "hold_expected": hold_expected,
        "hold_delivered": hold_delivered,
        "hold_missing": hold_missing,
    }
    summary = (
        f"missing_total={missing_total} hold_expected={hold_expected} "
        f"hold_delivered={hold_delivered} hold_missing={hold_missing}"
    )
    step(f"final success={success} hold_missing={hold_missing} ramp_missing={missing_total}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p09(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P09[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p09/{_unique_suffix()}"
    stable_topic = f"{root}/stable"
    step = _scenario_step_logger("P09")
    client_levels = list(
        _ramp_steps(
            int(parameters["start_clients"]),
            int(parameters["end_clients"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(client_levels))

    step(
        f"profile={config.size_profile} clients={parameters['start_clients']}-{parameters['end_clients']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(client_levels)} filters_per_client={parameters['filters_per_client']}")

    churn_clients: list[MqttClient] = []
    stable_sent = 0
    stable_received = 0
    try:
        with _connect_client(config, "p09-stable", timeout=max(config.timeout_seconds, 25.0)) as stable_subscriber:
            stable_subscriber.subscribe(stable_topic, qos=0)
            with _connect_client(config, "p09-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
                for level_index, target_clients in enumerate(client_levels, start=1):
                    while len(churn_clients) < target_clients:
                        churn_clients.append(_connect_client(config, f"p09-churn{len(churn_clients)}", timeout=max(config.timeout_seconds, 25.0)))

                    for client_index, churn_client in enumerate(churn_clients):
                        for filter_index in range(int(parameters["filters_per_client"])):
                            topic_filter = f"{root}/c{client_index}/f{filter_index}"
                            churn_client.subscribe(topic_filter, qos=0)
                            churn_client.unsubscribe(topic_filter)
                    if level_index in ramp_checkpoints:
                        progress = int((level_index / float(len(client_levels))) * 100.0)
                        step(f"ramp {progress}% churn_clients={len(churn_clients)}")
                    time.sleep(parameters["ramp_interval_s"])

                hold_deadline = time.monotonic() + max(1.0, hold_seconds)
                stable_per_round = 50
                step(f"hold start stable_per_round={stable_per_round} churn_clients={len(churn_clients)}")
                hold_started = time.monotonic()
                while time.monotonic() < hold_deadline:
                    for client_index, churn_client in enumerate(churn_clients):
                        topic_filter = f"{root}/c{client_index}/hold"
                        churn_client.subscribe(topic_filter, qos=0)
                        churn_client.unsubscribe(topic_filter)

                    for message_index in range(stable_per_round):
                        publisher.publish(stable_topic, f"p09-h-{message_index}".encode("utf-8"), qos=0)
                    stable_sent += stable_per_round
                    stable_received += _drain_until(stable_subscriber, stable_per_round, timeout_seconds=max(1.0, float(parameters["churn_interval_s"])))
                    ratio = (stable_received / stable_sent) if stable_sent > 0 else 0.0
                    step(f"hold sent={stable_sent} received={stable_received} ratio={ratio:.4f}")
                    time.sleep(float(parameters["churn_interval_s"]))

                    if hold_seconds <= 1.0:
                        break
                hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for churn_client in churn_clients:
            try:
                churn_client.disconnect()
            except BaseException:
                pass

    stable_ratio = (stable_received / stable_sent) if stable_sent > 0 else 0.0
    broker_alive = _broker_reachable(config.host, config.port, timeout_seconds=1.0)
    success = broker_alive and stable_ratio >= 0.99
    counters = {
        "stable_sent": stable_sent,
        "stable_received": stable_received,
        "churn_clients_peak": len(churn_clients),
        "broker_alive": 1 if broker_alive else 0,
    }
    summary = f"stable_sent={stable_sent} stable_received={stable_received} ratio={stable_ratio:.4f} broker_alive={broker_alive}"
    step(f"final success={success} ratio={stable_ratio:.4f} broker_alive={broker_alive}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(stable_received),
        throughput_per_second=_safe_rate(float(stable_received), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )


def _scenario_p10(config: RunnerConfig) -> ScenarioOutcome:
    parameters = PARAMS_P10[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p10/{_unique_suffix()}"
    will_topic = f"{root}/will"
    step = _scenario_step_logger("P10")
    client_levels = list(
        _ramp_steps(
            int(parameters["start_clients"]),
            int(parameters["end_clients"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(client_levels))

    step(
        f"profile={config.size_profile} will_clients={parameters['start_clients']}-{parameters['end_clients']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(client_levels)} will_qos={parameters['will_qos']}")

    hold_expected = 0
    hold_received = 0
    with _connect_client(config, "p10-watch", timeout=max(config.timeout_seconds, 25.0)) as watcher:
        watcher.subscribe(will_topic, qos=int(parameters["will_qos"]))

        for level_index, client_count in enumerate(client_levels, start=1):
            will_clients: list[MqttClient] = []
            for index in range(client_count):
                client = MqttClient(timeout_seconds=max(config.timeout_seconds, 25.0))
                client.set_will(
                    topic=will_topic,
                    payload=f"p10-r-{client_count}-{index}".encode("utf-8"),
                    qos=int(parameters["will_qos"]),
                    delay=int(parameters["will_delay_s"]),
                )
                client.connect(config.host, config.port, client_id=f"p10-r-{client_count}-{index}-{_unique_suffix()}")
                will_clients.append(client)

            for client in will_clients:
                _abrupt_disconnect(client)
            delivered = _drain_until(watcher, client_count, timeout_seconds=max(2.0, parameters["ramp_interval_s"] * 2.0))
            if level_index in ramp_checkpoints:
                progress = int((level_index / float(len(client_levels))) * 100.0)
                step(f"ramp {progress}% disconnected={client_count} wills={delivered}/{client_count}")
            time.sleep(parameters["ramp_interval_s"])

        hold_start = time.monotonic()
        hold_iteration = 0
        step(f"hold start end_clients={parameters['end_clients']}")
        while time.monotonic() - hold_start < max(1.0, hold_seconds):
            hold_clients: list[MqttClient] = []
            for index in range(int(parameters["end_clients"])):
                client = MqttClient(timeout_seconds=max(config.timeout_seconds, 25.0))
                client.set_will(
                    topic=will_topic,
                    payload=f"p10-h-{hold_iteration}-{index}".encode("utf-8"),
                    qos=int(parameters["will_qos"]),
                    delay=int(parameters["will_delay_s"]),
                )
                client.connect(config.host, config.port, client_id=f"p10-h-{hold_iteration}-{index}-{_unique_suffix()}")
                hold_clients.append(client)

            for client in hold_clients:
                _abrupt_disconnect(client)

            expected_this_round = int(parameters["end_clients"])
            hold_expected += expected_this_round
            hold_received += _drain_until(watcher, expected_this_round, timeout_seconds=max(2.0, hold_seconds / 2.0))
            ratio = (hold_received / hold_expected) if hold_expected > 0 else 0.0
            step(f"hold iter={hold_iteration + 1} received={hold_received}/{hold_expected} ratio={ratio:.4f}")
            hold_iteration += 1
            if hold_seconds <= 1.0:
                break

    ratio = (hold_received / hold_expected) if hold_expected > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_expected": hold_expected,
        "hold_received": hold_received,
        "hold_missing": max(0, hold_expected - hold_received),
    }
    summary = f"hold_expected={hold_expected} hold_received={hold_received} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_received),
        throughput_per_second=_safe_rate(float(hold_received), max(1e-6, time.monotonic() - hold_start)),
        throughput_basis_seconds=max(1e-6, time.monotonic() - hold_start),
    )


def _scenario_definitions() -> tuple[ScenarioDef, ...]:
    impl = {
        "S01": _scenario_s01,
        "P01": _scenario_p01,
        "P02": _scenario_p02,
        "P03": _scenario_p03,
        "P04": _scenario_p04,
        "P05": _scenario_p05,
        "P06": _scenario_p06,
        "P07": _scenario_p07,
        "P08": _scenario_p08,
        "P09": _scenario_p09,
        "P10": _scenario_p10,
    }
    return tuple(ScenarioDef(spec=spec, execute=impl[spec.scenario_id]) for spec in SCENARIO_SPECS)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run mqtt-broker performance scenarios")
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


if __name__ == "__main__":
    sys.exit(main())
