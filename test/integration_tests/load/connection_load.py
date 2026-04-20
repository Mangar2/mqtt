"""Integration tests for load section 18.1 (combined load test)."""

from __future__ import annotations

import errno
import importlib.util
from pathlib import Path
import socket
import time
import uuid
from contextlib import ExitStack


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[1] / "helpers" / f"{module_name}.py"
    spec = importlib.util.spec_from_file_location(f"integration_helper_{module_name}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name} from {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_broker_module = _load_helper("broker")
_raw_tcp_module = _load_helper("raw_tcp")
_mqtt_client_module = _load_helper("mqtt_client")
_assertions_module = _load_helper("assertions")

start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
MqttClient = _mqtt_client_module.MqttClient
assert_connack = _assertions_module.assert_connack
assert_reason_code = _assertions_module.assert_reason_code


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker(overrides: dict[str, object] | None = None):
    effective_overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _unique_topic(prefix: str) -> str:
    return f"integration/load/combined/{prefix}/{uuid.uuid4().hex}"


def _remaining_timeout(deadline_monotonic: float, *, minimum_seconds: float, cap_seconds: float) -> float:
    remaining_seconds = deadline_monotonic - time.monotonic()
    if remaining_seconds <= 0.0:
        raise TimeoutError("hard timeout reached")
    return max(minimum_seconds, min(cap_seconds, remaining_seconds))


def _connect_client(host: str, port: int, timeout_seconds: float, prefix: str) -> MqttClient:
    client = MqttClient(timeout_seconds=timeout_seconds)
    connack = client.connect(host, port, client_id=_unique_client_id(prefix), clean_start=True)
    assert_connack(connack, reason_code=0x00, session_present=False)
    return client


def _decode_remaining_length(first_octets: bytes, tcp_socket: socket.socket) -> tuple[int, int]:
    multiplier = 1
    value = 0
    bytes_used = 0

    for octet in first_octets:
        bytes_used += 1
        value += (octet & 0x7F) * multiplier
        if (octet & 0x80) == 0:
            return value, bytes_used
        multiplier *= 128

    while bytes_used < 4:
        octet = tcp_socket.recv(1)
        if not octet:
            raise RuntimeError("connection closed while decoding remaining length")
        current = int(octet[0])
        bytes_used += 1
        value += (current & 0x7F) * multiplier
        if (current & 0x80) == 0:
            return value, bytes_used
        multiplier *= 128

    raise RuntimeError("invalid MQTT remaining length encoding")


def _recv_exact(tcp_socket: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = tcp_socket.recv(size - len(chunks))
        if not chunk:
            raise RuntimeError("connection closed while receiving MQTT packet")
        chunks.extend(chunk)
    return bytes(chunks)


def _recv_packet(tcp_socket: socket.socket) -> tuple[int, int, bytes]:
    fixed_header = _recv_exact(tcp_socket, 2)
    first_byte = int(fixed_header[0])
    remaining_length, bytes_used = _decode_remaining_length(fixed_header[1:], tcp_socket)
    if bytes_used > 1:
        _ = _recv_exact(tcp_socket, bytes_used - 1)
    payload = _recv_exact(tcp_socket, remaining_length)
    return (first_byte >> 4) & 0x0F, first_byte & 0x0F, payload


def _parse_connack_reason(packet_type: int, payload: bytes) -> int:
    if packet_type != 2:
        raise RuntimeError(f"expected CONNACK packet (2), got {packet_type}")
    if len(payload) < 2:
        raise RuntimeError(f"invalid CONNACK payload length: {len(payload)}")
    return int(payload[1])


def _try_raise_nofile_limit(minimum_desired: int) -> int | None:
    try:
        import resource

        soft_limit, hard_limit = resource.getrlimit(resource.RLIMIT_NOFILE)
        target_soft_limit = soft_limit
        if soft_limit < minimum_desired:
            if hard_limit == resource.RLIM_INFINITY:
                target_soft_limit = minimum_desired
            else:
                target_soft_limit = min(hard_limit, minimum_desired)
            resource.setrlimit(resource.RLIMIT_NOFILE, (target_soft_limit, hard_limit))
        return int(target_soft_limit)
    except Exception:
        return None


def _execute_connection_load(
    host: str,
    port: int,
    *,
    total_connections: int,
    timeout_seconds: float,
    spacing_seconds: float,
    connect_batch_size: int = 0,
    batch_pause_seconds: float = 0.0,
    hold_after_connect_seconds: float = 0.0,
    allow_rejection: bool,
    deadline_monotonic: float,
) -> tuple[bool, str]:
    _try_raise_nofile_limit(total_connections + 128)
    open_sockets: list[socket.socket] = []
    success_count = 0
    rejection_codes: list[int] = []
    graceful_close_count = 0
    local_resource_errors = 0
    other_errors: list[str] = []

    try:
        for index in range(total_connections):
            if time.monotonic() >= deadline_monotonic:
                return (
                    False,
                    f"hard timeout reached during connection stage after {index}/{total_connections} attempts",
                )

            client_id = _unique_client_id(f"load-{total_connections}-{index}")
            connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
            tcp_socket: socket.socket | None = None
            try:
                operation_timeout_seconds = _remaining_timeout(
                    deadline_monotonic,
                    minimum_seconds=0.15,
                    cap_seconds=timeout_seconds,
                )
                tcp_socket = socket.create_connection((host, port), timeout=operation_timeout_seconds)
                tcp_socket.settimeout(operation_timeout_seconds)
                tcp_socket.sendall(connect_packet)

                packet_type, _packet_flags, payload = _recv_packet(tcp_socket)
                reason_code = _parse_connack_reason(packet_type, payload)
                if reason_code == 0x00:
                    open_sockets.append(tcp_socket)
                    tcp_socket = None
                    success_count += 1
                else:
                    rejection_codes.append(reason_code)
            except socket.timeout:
                other_errors.append("timeout while waiting for CONNACK")
            except OSError as error:
                if error.errno in {errno.EMFILE, errno.ENFILE}:
                    local_resource_errors += 1
                elif error.errno in {errno.ECONNRESET, errno.EPIPE, errno.ECONNREFUSED, errno.ETIMEDOUT}:
                    graceful_close_count += 1
                else:
                    other_errors.append(f"oserror {error.errno}: {error}")
            except RuntimeError as error:
                error_text = str(error)
                if "connection closed" in error_text.lower():
                    graceful_close_count += 1
                else:
                    other_errors.append(error_text)
            finally:
                if tcp_socket is not None:
                    try:
                        tcp_socket.close()
                    except OSError:
                        pass

            if (
                connect_batch_size > 0
                and batch_pause_seconds > 0.0
                and (index + 1) < total_connections
                and ((index + 1) % connect_batch_size) == 0
            ):
                if time.monotonic() >= deadline_monotonic:
                    return (
                        False,
                        f"hard timeout reached during batch pause after {index + 1}/{total_connections} attempts",
                    )
                time.sleep(
                    min(
                        batch_pause_seconds,
                        max(0.0, deadline_monotonic - time.monotonic()),
                    )
                )

            if spacing_seconds > 0.0:
                if time.monotonic() >= deadline_monotonic:
                    return (
                        False,
                        f"hard timeout reached during connection spacing after {index + 1}/{total_connections} attempts",
                    )
                time.sleep(min(spacing_seconds, max(0.0, deadline_monotonic - time.monotonic())))

        if hold_after_connect_seconds > 0.0 and success_count > 0:
            if time.monotonic() >= deadline_monotonic:
                return (
                    False,
                    f"hard timeout reached before hold phase after {success_count}/{total_connections} successful connects",
                )
            time.sleep(
                min(
                    hold_after_connect_seconds,
                    max(0.0, deadline_monotonic - time.monotonic()),
                )
            )

        non_success_total = rejection_codes.__len__() + graceful_close_count + local_resource_errors + other_errors.__len__()

        if not allow_rejection:
            if success_count != total_connections:
                error_samples = " | ".join(other_errors[:3]) if other_errors else "none"
                return (
                    False,
                    "expected all connections to succeed, "
                    f"success={success_count}/{total_connections}, "
                    f"connack_rejections={len(rejection_codes)}, graceful_closes={graceful_close_count}, "
                    f"local_resource_errors={local_resource_errors}, other_errors={len(other_errors)}, "
                    f"other_error_samples={error_samples}",
                )
            return True, f"all {total_connections} connections returned CONNACK success"

        if success_count + len(rejection_codes) + graceful_close_count != total_connections:
            error_samples = " | ".join(other_errors[:3]) if other_errors else "none"
            return (
                False,
                "encountered non-graceful connection failures under load, "
                f"success={success_count}, connack_rejections={len(rejection_codes)}, "
                f"graceful_closes={graceful_close_count}, local_resource_errors={local_resource_errors}, "
                f"other_errors={len(other_errors)}, other_error_samples={error_samples}",
            )

        if non_success_total == 0:
            return True, f"all {total_connections} connections succeeded"

        rejection_summary = (
            f"graceful outcome under pressure: success={success_count}, "
            f"connack_rejections={len(rejection_codes)}, graceful_closes={graceful_close_count}"
        )
        return True, rejection_summary
    finally:
        for tcp_socket in open_sockets:
            try:
                tcp_socket.close()
            except OSError:
                pass


def _execute_message_load_stage(
    host: str,
    port: int,
    *,
    message_count: int,
    timeout_seconds: float,
    deadline_monotonic: float,
    stage_label: str,
) -> tuple[bool, str]:
    topic = _unique_topic(f"throughput-{stage_label}")
    with _connect_client(host, port, timeout_seconds, f"sub-{stage_label}") as subscriber:
        suback_codes = subscriber.subscribe(topic, qos=1)
        if not suback_codes:
            return False, f"{stage_label} subscriber returned empty SUBACK"
        assert_reason_code(suback_codes[0], 0x01)

        with _connect_client(host, port, timeout_seconds, f"pub-{stage_label}") as publisher:
            for message_index in range(message_count):
                if time.monotonic() >= deadline_monotonic:
                    return False, f"{stage_label} hard timeout while publishing message {message_index}/{message_count}"
                payload = f"{stage_label}-msg-{message_index}".encode("utf-8")
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            collect_timeout_seconds = _remaining_timeout(
                deadline_monotonic,
                minimum_seconds=0.5,
                cap_seconds=max(timeout_seconds * 2.0, message_count / 150.0, 6.0),
            )
            received_messages = subscriber.collect_messages(count=message_count, timeout=collect_timeout_seconds)

        if len(received_messages) != message_count:
            return False, f"{stage_label} expected {message_count} messages, got {len(received_messages)}"

    return True, f"{stage_label} delivered {message_count}/{message_count} QoS1 messages"


def _execute_subscription_load_stage(
    host: str,
    port: int,
    *,
    subscription_count: int,
    timeout_seconds: float,
    deadline_monotonic: float,
    stage_label: str,
) -> tuple[bool, str]:
    topic_root = _unique_topic(f"subscription-{stage_label}")
    topics = [f"{topic_root}/{index}" for index in range(subscription_count)]

    with ExitStack() as stack:
        subscriber = stack.enter_context(_connect_client(host, port, timeout_seconds, f"sub-load-{stage_label}"))
        publisher = stack.enter_context(_connect_client(host, port, timeout_seconds, f"pub-load-{stage_label}"))

        for topic_index, topic_name in enumerate(topics):
            if time.monotonic() >= deadline_monotonic:
                return (
                    False,
                    f"{stage_label} hard timeout while subscribing topic {topic_index}/{subscription_count}",
                )
            suback_codes = subscriber.subscribe(topic_name, qos=0)
            if not suback_codes:
                return False, f"{stage_label} empty SUBACK for topic index {topic_index}"
            assert_reason_code(suback_codes[0], 0x00)

        for topic_index, topic_name in enumerate(topics):
            if time.monotonic() >= deadline_monotonic:
                return (
                    False,
                    f"{stage_label} hard timeout while publishing to topic {topic_index}/{subscription_count}",
                )
            payload = f"{stage_label}-topic-{topic_index}".encode("utf-8")
            publish_reason = publisher.publish(topic_name, payload, qos=0)
            assert_reason_code(publish_reason, 0x00)

        collect_timeout_seconds = _remaining_timeout(
            deadline_monotonic,
            minimum_seconds=0.5,
            cap_seconds=max(timeout_seconds * 2.0, subscription_count / 120.0, 8.0),
        )
        received_messages = subscriber.collect_messages(count=subscription_count, timeout=collect_timeout_seconds)
        received_topics = {message.topic for message in received_messages}

    expected_topics = set(topics)
    if received_topics != expected_topics:
        missing = len(expected_topics - received_topics)
        extra = len(received_topics - expected_topics)
        return False, f"{stage_label} subscription routing mismatch: missing={missing}, extra={extra}"

    return True, f"{stage_label} routed {subscription_count}/{subscription_count} subscribed topics"


def run_18_1_1_ten_concurrent_connections_all_connack_success(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        deadline_monotonic = time.monotonic() + max(20.0, config.timeout_seconds * 6.0)
        success, details = _execute_connection_load(
            host,
            port,
            total_connections=10,
            timeout_seconds=max(1.0, config.timeout_seconds),
            spacing_seconds=0.0,
            allow_rejection=False,
            deadline_monotonic=deadline_monotonic,
        )
        return success, f"18.1.1 {details}"
    except Exception as error:
        return False, f"18.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_18_1_2_progressive_combined_load_relative_steps_with_threshold(config) -> tuple[bool, str]:
    process = None
    stage_sizes: list[int] = []
    next_stage_size = 100
    while next_stage_size <= 12800:
        stage_sizes.append(next_stage_size)
        next_stage_size *= 2

    success_threshold_stage = 800
    highest_successful_stage = 0
    stage_reports: list[str] = []
    hard_timeout_seconds = max(180.0, config.timeout_seconds * 45.0)

    try:
        host, port, process = _start_isolated_broker()
        deadline_monotonic = time.monotonic() + hard_timeout_seconds

        for stage_size in stage_sizes:
            stage_label = f"18.1.2/stage-{stage_size}"

            connection_start = time.monotonic()
            try:
                connection_ok, connection_details = _execute_connection_load(
                    host,
                    port,
                    total_connections=stage_size,
                    timeout_seconds=max(0.6, min(config.timeout_seconds, 1.5)),
                    spacing_seconds=0.0,
                    connect_batch_size=5,
                    batch_pause_seconds=0.04,
                    hold_after_connect_seconds=0.25,
                    allow_rejection=True,
                    deadline_monotonic=deadline_monotonic,
                )
            except Exception as stage_error:
                return (
                    False,
                    "18.1.2 failed during connection stage "
                    f"at {stage_size}: {stage_error}; highest_successful_stage={highest_successful_stage}; "
                    f"stage_reports={stage_reports}",
                )
            if not connection_ok:
                if highest_successful_stage >= success_threshold_stage:
                    return (
                        True,
                        "18.1.2 success threshold met (800) and stopped after first failing stage "
                        f"{stage_size} in connection phase: {connection_details}; "
                        f"highest_successful_stage={highest_successful_stage}; stage_reports={stage_reports}",
                    )
                return (
                    False,
                    "18.1.2 failed during connection stage "
                    f"at {stage_size}: {connection_details}; highest_successful_stage={highest_successful_stage}; "
                    "stopped immediately (no n+1 stage); "
                    f"stage_reports={stage_reports}",
                )

            try:
                message_ok, message_details = _execute_message_load_stage(
                    host,
                    port,
                    message_count=stage_size,
                    timeout_seconds=max(1.0, config.timeout_seconds),
                    deadline_monotonic=deadline_monotonic,
                    stage_label=stage_label,
                )
            except Exception as stage_error:
                return (
                    False,
                    "18.1.2 failed during message stage "
                    f"at {stage_size}: {stage_error}; highest_successful_stage={highest_successful_stage}; "
                    f"stage_reports={stage_reports}",
                )
            if not message_ok:
                if highest_successful_stage >= success_threshold_stage:
                    return (
                        True,
                        "18.1.2 success threshold met (800) and stopped after first failing stage "
                        f"{stage_size} in message phase: {message_details}; "
                        f"highest_successful_stage={highest_successful_stage}; stage_reports={stage_reports}",
                    )
                return (
                    False,
                    "18.1.2 failed during message stage "
                    f"at {stage_size}: {message_details}; highest_successful_stage={highest_successful_stage}; "
                    "stopped immediately (no n+1 stage); "
                    f"stage_reports={stage_reports}",
                )

            try:
                subscription_ok, subscription_details = _execute_subscription_load_stage(
                    host,
                    port,
                    subscription_count=stage_size,
                    timeout_seconds=max(1.0, config.timeout_seconds),
                    deadline_monotonic=deadline_monotonic,
                    stage_label=stage_label,
                )
            except Exception as stage_error:
                return (
                    False,
                    "18.1.2 failed during subscription stage "
                    f"at {stage_size}: {stage_error}; highest_successful_stage={highest_successful_stage}; "
                    f"stage_reports={stage_reports}",
                )
            if not subscription_ok:
                if highest_successful_stage >= success_threshold_stage:
                    return (
                        True,
                        "18.1.2 success threshold met (800) and stopped after first failing stage "
                        f"{stage_size} in subscription phase: {subscription_details}; "
                        f"highest_successful_stage={highest_successful_stage}; stage_reports={stage_reports}",
                    )
                return (
                    False,
                    "18.1.2 failed during subscription stage "
                    f"at {stage_size}: {subscription_details}; highest_successful_stage={highest_successful_stage}; "
                    "stopped immediately (no n+1 stage); "
                    f"stage_reports={stage_reports}",
                )

            highest_successful_stage = stage_size
            stage_reports.append(
                f"{stage_size}: conn[{connection_details}] msg[{message_details}] sub[{subscription_details}]"
            )

        if highest_successful_stage < success_threshold_stage:
            return (
                False,
                "18.1.2 failed: success threshold 800 was not reached; "
                f"highest_successful_stage={highest_successful_stage}; stage_reports={stage_reports}",
            )

        return (
            True,
            "18.1.2 progressive combined load reached full range up to 12800 "
            f"within hard timeout {hard_timeout_seconds:.1f}s; stage_reports={stage_reports}",
        )
    except Exception as error:
        return False, f"18.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_18_1_3_connection_storm_hundred_clients_within_one_second(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        deadline_monotonic = time.monotonic() + max(20.0, config.timeout_seconds * 6.0)
        start_time = time.monotonic()
        success, details = _execute_connection_load(
            host,
            port,
            total_connections=100,
            timeout_seconds=max(1.0, config.timeout_seconds),
            spacing_seconds=0.0,
            allow_rejection=False,
            deadline_monotonic=deadline_monotonic,
        )
        elapsed = time.monotonic() - start_time
        if not success:
            return False, f"18.1.3 connection storm failed: {details}"
        if elapsed > 1.0:
            return False, f"18.1.3 storm exceeded 1 second target: {elapsed:.3f}s"

        probe_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("probe-18-1-3")))
        with socket.create_connection((host, port), timeout=max(1.0, config.timeout_seconds)) as probe_socket:
            probe_socket.settimeout(max(1.0, config.timeout_seconds))
            probe_socket.sendall(probe_packet)
            packet_type, _packet_flags, payload = _recv_packet(probe_socket)
            reason = _parse_connack_reason(packet_type, payload)
            if reason != 0x00:
                return False, f"18.1.3 probe connection was not accepted after storm: reason=0x{reason:02X}"

        return True, f"18.1.3 completed 100 connections in {elapsed:.3f}s; broker remained responsive"
    except Exception as error:
        return False, f"18.1.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "load/connection_load_10_concurrent_all_connack_success",
        "description": "18.1.1 10 concurrent connections -> all CONNACK success",
        "run": run_18_1_1_ten_concurrent_connections_all_connack_success,
    },
    {
        "name": "load/combined_progressive_stages_up_to_1000_with_timeout",
        "description": "18.1.2 Combined progressive load with relative stages (*2) up to 12800 and hard timeout; success threshold is stage 800",
        "run": run_18_1_2_progressive_combined_load_relative_steps_with_threshold,
    },
    {
        "name": "load/connection_storm_100_within_one_second_no_crash",
        "description": "18.1.3 Connection storm: 100 clients connect within 1 second -> no crash, all handled",
        "run": run_18_1_3_connection_storm_hundred_clients_within_one_second,
    },
]
