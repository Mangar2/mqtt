"""Integration tests for robustness section 19.2 (Connection Abuse)."""

from __future__ import annotations

import errno
import importlib.util
from pathlib import Path
import socket
import struct
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


_assertions_module = _load_helper("assertions")
_broker_module = _load_helper("broker")
_mqtt_client_module = _load_helper("mqtt_client")
_raw_tcp_module = _load_helper("raw_tcp")

assert_connack = _assertions_module.assert_connack
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
open_idle_connection = _raw_tcp_module.open_idle_connection


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


def _verify_valid_connect(host: str, port: int, timeout_seconds: float, prefix: str = "robustness-verify") -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as client:
        connack = client.connect(
            host,
            port,
            client_id=_unique_client_id(prefix),
            clean_start=True,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)


def _decode_remaining_length(tcp_socket: socket.socket) -> int:
    multiplier = 1
    value = 0
    for index in range(4):
        octet = tcp_socket.recv(1)
        if octet == b"":
            raise RuntimeError("connection closed while decoding remaining length")
        if not octet:
            raise RuntimeError("missing remaining length byte")
        byte_value = int(octet[0])
        value += (byte_value & 0x7F) * multiplier
        if (byte_value & 0x80) == 0:
            return value
        multiplier *= 128
        if index == 3:
            raise RuntimeError("invalid MQTT remaining length encoding")
    raise RuntimeError("unreachable")


def _recv_exact(tcp_socket: socket.socket, expected_size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < expected_size:
        chunk = tcp_socket.recv(expected_size - len(chunks))
        if chunk == b"":
            raise RuntimeError("connection closed while receiving packet bytes")
        chunks.extend(chunk)
    return bytes(chunks)


def _recv_mqtt_packet(tcp_socket: socket.socket) -> tuple[int, int, bytes]:
    first = _recv_exact(tcp_socket, 1)
    first_byte = int(first[0])
    packet_type = (first_byte >> 4) & 0x0F
    packet_flags = first_byte & 0x0F
    remaining_length = _decode_remaining_length(tcp_socket)
    payload = _recv_exact(tcp_socket, remaining_length)
    return packet_type, packet_flags, payload


def _wait_for_peer_close(tcp_socket: socket.socket, timeout_seconds: float) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            probe = tcp_socket.recv(1)
            if probe == b"":
                return True
        except socket.timeout:
            continue
        except (ConnectionResetError, BrokenPipeError):
            return True
    return False


def run_19_2_1_idle_tcp_connection_timeout_close(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))
        idle_observation_seconds = max(30.0, min(45.0, timeout * 4.0))

        result = open_idle_connection(host, port, duration_seconds=idle_observation_seconds)
        if not bool(result.get("closed_by_peer", False)):
            return (
                False,
                "broker did not close idle TCP connection without CONNECT "
                f"within {idle_observation_seconds:.1f}s",
            )

        _verify_valid_connect(host, port, timeout)
        return True, "19.2.1 broker closed idle TCP connection and remained healthy"
    except Exception as error:
        return False, f"19.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_19_2_2_partial_connect_timeout_clean_close(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))
        observe_seconds = max(30.0, min(45.0, timeout * 4.0))

        connect_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("partial-connect")))
        partial_connect = connect_packet[: max(2, len(connect_packet) - 2)]
        with socket.create_connection((host, port), timeout=timeout) as tcp_socket:
            tcp_socket.settimeout(0.2)
            tcp_socket.sendall(partial_connect)
            closed = _wait_for_peer_close(tcp_socket, observe_seconds)
            if not closed:
                return (
                    False,
                    "broker did not close connection after partial CONNECT "
                    f"within {observe_seconds:.1f}s",
                )

        _verify_valid_connect(host, port, timeout)
        return True, "19.2.2 broker timed out partial CONNECT and closed cleanly"
    except Exception as error:
        return False, f"19.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_19_2_3_connect_then_invalid_packet_flood_disconnect(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        connect_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("invalid-flood")))
        with socket.create_connection((host, port), timeout=timeout) as tcp_socket:
            tcp_socket.settimeout(timeout)
            tcp_socket.sendall(connect_packet)
            packet_type, _packet_flags, payload = _recv_mqtt_packet(tcp_socket)
            if packet_type != 2:
                return False, f"expected CONNACK packet type 2, got {packet_type}"
            if len(payload) < 2 or int(payload[1]) != 0x00:
                return False, f"expected CONNACK success reason 0x00, got payload={payload!r}"

            invalid_flood = b"\xff\x00" * 1024
            tcp_socket.sendall(invalid_flood)
            tcp_socket.settimeout(0.2)
            closed = _wait_for_peer_close(tcp_socket, timeout)
            if not closed:
                return False, "broker did not disconnect client after invalid packet flood"

        _verify_valid_connect(host, port, timeout)
        return True, "19.2.3 broker disconnected invalid packet flood client and stayed stable"
    except Exception as error:
        return False, f"19.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_19_2_4_connect_then_immediate_rst_cleanup(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        connect_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("rst-client")))
        tcp_socket = socket.create_connection((host, port), timeout=timeout)
        try:
            tcp_socket.settimeout(timeout)
            tcp_socket.sendall(connect_packet)
            packet_type, _packet_flags, payload = _recv_mqtt_packet(tcp_socket)
            if packet_type != 2:
                return False, f"expected CONNACK packet type 2 before RST, got {packet_type}"
            if len(payload) < 2 or int(payload[1]) != 0x00:
                return False, f"expected CONNACK success before RST, got payload={payload!r}"

            tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        finally:
            tcp_socket.close()

        for _ in range(10):
            _verify_valid_connect(host, port, timeout, prefix="post-rst-verify")
        return True, "19.2.4 broker recovered after client-side TCP RST without visible leak"
    except Exception as error:
        return False, f"19.2.4 failed: {error}"
    finally:
        stop_broker(process)


def run_19_2_5_many_half_open_connections_no_deadlock(config) -> tuple[bool, str]:
    process = None
    sockets: list[socket.socket] = []
    try:
        host, port, process = _start_isolated_broker(
            overrides={
                "broker.max_connections": 2000,
            }
        )
        timeout = max(1.0, float(config.timeout_seconds))
        target_connections = 100

        for _ in range(target_connections):
            try:
                tcp_socket = socket.create_connection((host, port), timeout=timeout)
                tcp_socket.settimeout(timeout)
                sockets.append(tcp_socket)
            except OSError as error:
                if error.errno in {errno.EMFILE, errno.ENFILE}:
                    return False, f"local test host file-descriptor limit reached after {len(sockets)} connections"
                return False, f"failed to open idle TCP connection #{len(sockets) + 1}: {error}"

        if len(sockets) != target_connections:
            return False, f"expected {target_connections} idle TCP connections, got {len(sockets)}"

        _verify_valid_connect(host, port, timeout, prefix="half-open-verify")
        return True, "19.2.5 broker accepted 100 idle TCP connections and remained responsive"
    except Exception as error:
        return False, f"19.2.5 failed: {error}"
    finally:
        for tcp_socket in sockets:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


TEST_CASES = [
    {
        "name": "robustness/connection_abuse_idle_tcp_timeout",
        "description": "19.2.1 Client opens TCP connection, sends nothing for 30s, broker closes",
        "run": run_19_2_1_idle_tcp_connection_timeout_close,
    },
    {
        "name": "robustness/connection_abuse_partial_connect_timeout",
        "description": "19.2.2 Client opens TCP connection, sends partial CONNECT, timeout then clean close",
        "run": run_19_2_2_partial_connect_timeout_clean_close,
    },
    {
        "name": "robustness/connection_abuse_invalid_packet_flood",
        "description": "19.2.3 Client sends CONNECT then flood of invalid packets, broker disconnects and stays stable",
        "run": run_19_2_3_connect_then_invalid_packet_flood_disconnect,
    },
    {
        "name": "robustness/connection_abuse_connect_then_rst",
        "description": "19.2.4 Client sends CONNECT then immediate TCP RST, broker cleans up resources",
        "run": run_19_2_4_connect_then_immediate_rst_cleanup,
    },
    {
        "name": "robustness/connection_abuse_many_half_open_connections",
        "description": "19.2.5 100 half-open TCP connections with no data, broker remains responsive without deadlock",
        "run": run_19_2_5_many_half_open_connections_no_deadlock,
    },
]
