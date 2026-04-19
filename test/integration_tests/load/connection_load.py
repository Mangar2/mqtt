"""Integration tests for load section 18.1 (connection load)."""

from __future__ import annotations

import errno
import importlib.util
from pathlib import Path
import socket
import time
import uuid


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

start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


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
    return "127.0.0.1", int(effective_overrides["network.mqtt_port"]), process


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
    allow_rejection: bool,
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
            client_id = _unique_client_id(f"load-{total_connections}-{index}")
            connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
            tcp_socket: socket.socket | None = None
            try:
                tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
                tcp_socket.settimeout(timeout_seconds)
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

            if spacing_seconds > 0.0:
                time.sleep(spacing_seconds)

        non_success_total = rejection_codes.__len__() + graceful_close_count + local_resource_errors + other_errors.__len__()

        if not allow_rejection:
            if success_count != total_connections:
                return (
                    False,
                    "expected all connections to succeed, "
                    f"success={success_count}/{total_connections}, "
                    f"connack_rejections={len(rejection_codes)}, graceful_closes={graceful_close_count}, "
                    f"local_resource_errors={local_resource_errors}, other_errors={len(other_errors)}",
                )
            return True, f"all {total_connections} connections returned CONNACK success"

        if success_count + len(rejection_codes) + graceful_close_count != total_connections:
            return (
                False,
                "encountered non-graceful connection failures under load, "
                f"success={success_count}, connack_rejections={len(rejection_codes)}, "
                f"graceful_closes={graceful_close_count}, local_resource_errors={local_resource_errors}, "
                f"other_errors={len(other_errors)}",
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


def run_18_1_1_hundred_concurrent_connections_all_connack_success(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        return _execute_connection_load(
            host,
            port,
            total_connections=100,
            timeout_seconds=max(1.0, config.timeout_seconds),
            spacing_seconds=0.0,
            allow_rejection=False,
        )
    except Exception as error:
        return False, f"18.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_18_1_2_five_hundred_concurrent_connections_all_connack_success(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        return _execute_connection_load(
            host,
            port,
            total_connections=500,
            timeout_seconds=max(1.2, config.timeout_seconds),
            spacing_seconds=0.0005,
            allow_rejection=False,
        )
    except Exception as error:
        return False, f"18.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_18_1_3_thousand_concurrent_connections_success_or_graceful_rejection(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        return _execute_connection_load(
            host,
            port,
            total_connections=1000,
            timeout_seconds=max(1.2, config.timeout_seconds),
            spacing_seconds=0.0005,
            allow_rejection=True,
        )
    except Exception as error:
        return False, f"18.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_18_1_4_connection_storm_hundred_clients_within_one_second(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        start_time = time.monotonic()
        success, details = _execute_connection_load(
            host,
            port,
            total_connections=100,
            timeout_seconds=max(1.0, config.timeout_seconds),
            spacing_seconds=0.0,
            allow_rejection=False,
        )
        elapsed = time.monotonic() - start_time
        if not success:
            return False, f"18.1.4 connection storm failed: {details}"
        if elapsed > 1.0:
            return False, f"18.1.4 storm exceeded 1 second target: {elapsed:.3f}s"

        probe_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("probe-18-1-4")))
        with socket.create_connection((host, port), timeout=max(1.0, config.timeout_seconds)) as probe_socket:
            probe_socket.settimeout(max(1.0, config.timeout_seconds))
            probe_socket.sendall(probe_packet)
            packet_type, _packet_flags, payload = _recv_packet(probe_socket)
            reason = _parse_connack_reason(packet_type, payload)
            if reason != 0x00:
                return False, f"18.1.4 probe connection was not accepted after storm: reason=0x{reason:02X}"

        return True, f"18.1.4 completed 100 connections in {elapsed:.3f}s; broker remained responsive"
    except Exception as error:
        return False, f"18.1.4 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "load/connection_load_100_concurrent_all_connack_success",
        "description": "18.1.1 100 concurrent connections -> all CONNACK success",
        "run": run_18_1_1_hundred_concurrent_connections_all_connack_success,
    },
    {
        "name": "load/connection_load_500_concurrent_all_connack_success",
        "description": "18.1.2 500 concurrent connections -> all CONNACK success",
        "run": run_18_1_2_five_hundred_concurrent_connections_all_connack_success,
    },
    {
        "name": "load/connection_load_1000_concurrent_success_or_graceful_rejection",
        "description": "18.1.3 1000 concurrent connections -> all CONNACK success or graceful rejection",
        "run": run_18_1_3_thousand_concurrent_connections_success_or_graceful_rejection,
    },
    {
        "name": "load/connection_storm_100_within_one_second_no_crash",
        "description": "18.1.4 Connection storm: 100 clients connect within 1 second -> no crash, all handled",
        "run": run_18_1_4_connection_storm_hundred_clients_within_one_second,
    },
]
