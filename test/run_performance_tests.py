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
    ScenarioSpec(2, "S02", "connection-storm", "100 fast connects under burst load", "100/100 successful connects", 100.0, "conn"),
    ScenarioSpec(3, "S03", "qos0-throughput", "single pub/sub throughput", "1000 msgs under target seconds", 1000.0, "msg"),
    ScenarioSpec(4, "S04", "qos1-throughput", "single pub/sub QoS1 reliability", "1000 msgs acked and delivered", 1000.0, "msg"),
    ScenarioSpec(5, "S05", "fanout-medium", "multi pub/sub fanout load", "all expected fanout deliveries", 1250.0, "delivery"),
    ScenarioSpec(6, "S06", "subscriptions-single-client", "single client high filter count", "all subscriptions routable", 300.0, "sub"),
    ScenarioSpec(7, "S07", "subscriptions-many-clients", "many clients filter routing", "all routes match clients", 100.0, "sub"),
    ScenarioSpec(8, "S08", "combined-progressive", "progressive mixed load", "all phases complete", 200.0, "op"),
    ScenarioSpec(9, "S09A", "sustained-retained-store", "retained replay completeness", "all retained entries replayed", 300.0, "entry"),
    ScenarioSpec(9, "S09B", "sustained-offline-queue", "offline queued redelivery", "all queued messages redelivered", 200.0, "msg"),
    ScenarioSpec(10, "S10", "sustained-long-run", "mixed traffic over runtime window", "no disconnect and no loss in window", None, None),
)


SIZE_STAGE_RANGES: dict[str, tuple[int, int]] = {
    "small": (1, 3),
    "middle": (3, 5),
    "large": (5, 10),
}


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


def _scenario_connection_attempts(config: RunnerConfig, total_attempts: int) -> ScenarioOutcome:
    counters = {
        "success": 0,
        "rejected": 0,
        "network_close": 0,
        "local_resource": 0,
        "other": 0,
    }
    for index in range(total_attempts):
        category, _detail = _tcp_connect_trial(config, f"perf-{index}-{_unique_suffix()}")
        counters[category] += 1

    success = counters["success"] == total_attempts
    summary = (
        f"connects success={counters['success']}/{total_attempts} "
        f"reject={counters['rejected']} net_close={counters['network_close']} "
        f"local_res={counters['local_resource']} other={counters['other']}"
    )
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(total_attempts))


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


def _scenario_s02(config: RunnerConfig) -> ScenarioOutcome:
    return _scenario_connection_attempts(config, total_attempts=100)


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


def _scenario_qos_throughput(config: RunnerConfig, qos: int, count: int, target_seconds: float | None) -> ScenarioOutcome:
    topic = f"perf/{'qos1' if qos == 1 else 'qos0'}/{_unique_suffix()}"
    timeout = max(config.timeout_seconds, 15.0)
    with _connect_client(config, "sub", timeout=timeout) as subscriber:
        suback = subscriber.subscribe(topic, qos=qos)
        if not suback:
            raise RuntimeError("missing SUBACK")
        with _connect_client(config, "pub", timeout=timeout) as publisher:
            start = time.monotonic()
            for index in range(count):
                reason = int(publisher.publish(topic, f"m-{index}".encode("utf-8"), qos=qos))
                if reason not in (0x00, 0x10):
                    raise RuntimeError(f"publish failed reason={reason}")
            received = subscriber.collect_messages(count=count, timeout=max(20.0, config.timeout_seconds * 3.0))
            elapsed = time.monotonic() - start

    delivered = len(received)
    counters = {"sent": count, "delivered": delivered, "lost": max(0, count - delivered)}
    success = delivered == count
    if target_seconds is not None:
        success = success and elapsed < target_seconds

    limit_text = "none"
    if target_seconds is not None:
        limit_text = f"target_s={target_seconds:.1f}"
    summary = f"sent={count} delivered={delivered} elapsed_s={elapsed:.3f} {limit_text}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(count))


def _scenario_s03(config: RunnerConfig) -> ScenarioOutcome:
    target_seconds = 5.0 if _is_local_host(config.host) else 15.0
    return _scenario_qos_throughput(config, qos=0, count=1000, target_seconds=target_seconds)


def _scenario_s04(config: RunnerConfig) -> ScenarioOutcome:
    return _scenario_qos_throughput(config, qos=1, count=1000, target_seconds=None)


def _scenario_s05(config: RunnerConfig) -> ScenarioOutcome:
    publishers = 5
    subscribers_count = 5
    per_publisher = 50
    expected_publications = publishers * per_publisher
    expected_deliveries = expected_publications * subscribers_count
    topic = f"perf/fanout/{_unique_suffix()}"

    timeout = max(config.timeout_seconds, 20.0)
    clients = []
    try:
        subscribers = [_connect_client(config, f"sub{i}", timeout=timeout) for i in range(subscribers_count)]
        clients.extend(subscribers)
        publishers_clients = [_connect_client(config, f"pub{i}", timeout=timeout) for i in range(publishers)]
        clients.extend(publishers_clients)

        for subscriber in subscribers:
            suback = subscriber.subscribe(topic, qos=1)
            if not suback:
                raise RuntimeError("missing SUBACK in fanout")

        for publisher_index, publisher in enumerate(publishers_clients):
            for message_index in range(per_publisher):
                payload = f"p{publisher_index}-m{message_index}".encode("utf-8")
                publisher.publish(topic, payload, qos=1)

        delivery_total = 0
        for subscriber in subscribers:
            got = subscriber.collect_messages(count=expected_publications, timeout=max(25.0, timeout * 2.0))
            delivery_total += len(got)
    finally:
        for client in clients:
            try:
                client.disconnect()
            except BaseException:
                pass

    counters = {
        "publications": expected_publications,
        "expected_deliveries": expected_deliveries,
        "actual_deliveries": delivery_total,
    }
    success = delivery_total == expected_deliveries
    summary = f"publications={expected_publications} deliveries={delivery_total}/{expected_deliveries}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(expected_deliveries))


def _scenario_s06(config: RunnerConfig) -> ScenarioOutcome:
    filter_count = 300
    prefix = f"perf/sub-single/{_unique_suffix()}"
    with _connect_client(config, "sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
        for index in range(filter_count):
            subscriber.subscribe(f"{prefix}/{index}", qos=0)

        with _connect_client(config, "pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            for index in range(filter_count):
                publisher.publish(f"{prefix}/{index}", f"v-{index}".encode("utf-8"), qos=0)

        messages = subscriber.collect_messages(count=filter_count, timeout=max(25.0, config.timeout_seconds * 3.0))
        delivered_topics = {msg.topic for msg in messages}

    success = len(delivered_topics) == filter_count
    counters = {"filters": filter_count, "delivered_topics": len(delivered_topics)}
    summary = f"filters={filter_count} delivered_topics={len(delivered_topics)}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(filter_count))


def _scenario_s07(config: RunnerConfig) -> ScenarioOutcome:
    clients_count = 20
    filters_per_client = 5
    prefix = f"perf/sub-many/{_unique_suffix()}"

    subscribers = []
    try:
        subscribers = [_connect_client(config, f"sub{idx}", timeout=max(config.timeout_seconds, 20.0)) for idx in range(clients_count)]
        with _connect_client(config, "pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            topic_map: list[list[str]] = []
            for client_index, subscriber in enumerate(subscribers):
                topics: list[str] = []
                for filter_index in range(filters_per_client):
                    topic = f"{prefix}/c{client_index}/f{filter_index}"
                    topics.append(topic)
                    subscriber.subscribe(topic, qos=0)
                topic_map.append(topics)

            for topics in topic_map:
                for topic in topics:
                    publisher.publish(topic, b"x", qos=0)

            mismatches = 0
            for client_index, subscriber in enumerate(subscribers):
                got = subscriber.collect_messages(count=filters_per_client, timeout=max(20.0, config.timeout_seconds * 3.0))
                got_topics = {message.topic for message in got}
                if got_topics != set(topic_map[client_index]):
                    mismatches += 1
    finally:
        for client in subscribers:
            try:
                client.disconnect()
            except BaseException:
                pass

    expected_subs = clients_count * filters_per_client
    success = mismatches == 0
    counters = {"subscriptions": expected_subs, "mismatched_clients": mismatches}
    summary = f"subscriptions={expected_subs} mismatched_clients={mismatches}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(expected_subs))


def _scenario_s08(config: RunnerConfig) -> ScenarioOutcome:
    phases = [20, 40, 60, 80]
    total_ops = 0
    for attempts in phases:
        outcome = _scenario_connection_attempts(config, attempts)
        total_ops += attempts
        if not outcome.success:
            return ScenarioOutcome(
                success=False,
                summary=f"phase_attempts={attempts} failed: {outcome.summary}",
                counters={"failed_phase": attempts, **outcome.counters},
                work_units=float(total_ops),
            )
    return ScenarioOutcome(success=True, summary=f"phases_ok attempts_total={total_ops}", counters={"attempts": total_ops}, work_units=float(total_ops))


def _scenario_s09a(config: RunnerConfig) -> ScenarioOutcome:
    count = 300
    root = f"perf/retained/{_unique_suffix()}"
    expected: dict[str, bytes] = {}
    with _connect_client(config, "pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
        for index in range(count):
            topic = f"{root}/{index}"
            payload = f"ret-{index}".encode("utf-8")
            expected[topic] = payload
            publisher.publish(topic, payload, qos=1, retain=True)

    with _connect_client(config, "sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
        subscriber.subscribe(f"{root}/#", qos=0)
        messages = subscriber.collect_messages(count=count, timeout=max(25.0, config.timeout_seconds * 3.0))
        got = {message.topic: bytes(message.payload) for message in messages}

    success = got == expected
    missing = len(set(expected.keys()) - set(got.keys()))
    mismatched = sum(1 for topic, payload in expected.items() if topic in got and got[topic] != payload)
    counters = {"expected": count, "received": len(got), "missing": missing, "payload_mismatch": mismatched}
    summary = f"retained expected={count} received={len(got)} missing={missing} mismatch={mismatched}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(count))


def _scenario_s09b(config: RunnerConfig) -> ScenarioOutcome:
    queued = 200
    topic = f"perf/offline/{_unique_suffix()}"
    client_id = f"offline-{_unique_suffix()}"

    connect_props = Properties(PacketTypes.CONNECT)
    setattr(connect_props, "SessionExpiryInterval", 300)

    with _connect_client(config, "sub-init", clean_start=True, client_id=client_id, properties=connect_props) as sub_init:
        sub_init.subscribe(topic, qos=1)

    with _connect_client(config, "pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
        for index in range(queued):
            publisher.publish(topic, f"q-{index}".encode("utf-8"), qos=1)

    reconnect_props = Properties(PacketTypes.CONNECT)
    setattr(reconnect_props, "SessionExpiryInterval", 300)

    with _connect_client(config, "sub-resume", clean_start=False, client_id=client_id, properties=reconnect_props) as resumed:
        messages = resumed.collect_messages(count=queued, timeout=max(30.0, config.timeout_seconds * 4.0))
        delivered = {bytes(message.payload) for message in messages}
    expected = {f"q-{index}".encode("utf-8") for index in range(queued)}

    missing = len(expected - delivered)
    success = missing == 0
    counters = {"queued": queued, "delivered": len(delivered), "missing": missing}
    summary = f"offline queued={queued} delivered={len(delivered)} missing={missing}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(queued))


def _scenario_s10(config: RunnerConfig) -> ScenarioOutcome:
    runtime_seconds = 20.0
    clients_count = 10
    root = f"perf/sustained/{_unique_suffix()}"

    clients = []
    try:
        clients = [_connect_client(config, f"s{index}", timeout=max(config.timeout_seconds, 20.0)) for index in range(clients_count)]
        topics = [f"{root}/{index}" for index in range(clients_count)]
        for index, client in enumerate(clients):
            client.subscribe(topics[index], qos=0)

        sent = 0
        deadline = time.monotonic() + runtime_seconds
        while time.monotonic() < deadline:
            for index, client in enumerate(clients):
                dst = (index + 1) % clients_count
                client.publish(topics[dst], f"s-{sent}".encode("utf-8"), qos=0)
                sent += 1

            for client in clients:
                drained = client.drain_available_messages(limit=4)
                if not drained:
                    raise RuntimeError("no message received during sustained window")

        alive = _broker_reachable(config.host, config.port, timeout_seconds=1.0)
    finally:
        for client in clients:
            try:
                client.disconnect()
            except BaseException:
                pass

    success = alive
    counters = {"sent": sent, "broker_alive": 1 if alive else 0}
    summary = f"runtime_s={runtime_seconds:.0f} sent={sent} broker_alive={alive}"
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=None)


def _scenario_definitions() -> tuple[ScenarioDef, ...]:
    impl = {
        "S01": _scenario_s01,
        "S02": _scenario_s02,
        "S03": _scenario_s03,
        "S04": _scenario_s04,
        "S05": _scenario_s05,
        "S06": _scenario_s06,
        "S07": _scenario_s07,
        "S08": _scenario_s08,
        "S09A": _scenario_s09a,
        "S09B": _scenario_s09b,
        "S10": _scenario_s10,
    }
    return tuple(ScenarioDef(spec=spec, execute=impl[spec.scenario_id]) for spec in SCENARIO_SPECS)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run mqtt-broker performance scenarios")
    parser.add_argument("--host", default="127.0.0.1", help="Broker hostname (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883, help="Broker TCP port (default: 1883)")
    parser.add_argument("--ws-port", type=int, default=None, help="Broker WebSocket port")
    parser.add_argument("--timeout", type=float, default=8.0, help="Base timeout in seconds")
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
            try:
                outcome = definition.execute(config)
                summary = outcome.summary
                counters = dict(outcome.counters)
                if not outcome.success:
                    status = "DEVIATION"
                if outcome.work_units is not None:
                    elapsed = max(time.perf_counter() - started_perf, 1e-6)
                    throughput = outcome.work_units / elapsed
            except BaseException as error:  # pylint: disable=broad-except
                status = "ABORTED"
                summary = str(error)

            duration_seconds = time.perf_counter() - started_perf
            finished_at = _now_utc_iso()
            rate_text = "n/a"
            if throughput is not None and spec.work_unit_label is not None:
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
    print("=== MQTT Performance Run ===")
    print(f"target   : {config.host}:{config.port}")
    print(f"profile  : {args.size} stages={start_stage}-{end_stage}")
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
    print(f"runtime   : {total_seconds:.3f}s")
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
