"""Integration tests for connection lifecycle section 1.7 (Keep Alive)."""

from __future__ import annotations

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


_assertions_module = _load_helper("assertions")
_broker_module = _load_helper("broker")
_raw_tcp_module = _load_helper("raw_tcp")

assert_reason_code = _assertions_module.assert_reason_code
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


def _read_exact_bytes(tcp_socket: socket.socket, expected_size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < expected_size:
        received = tcp_socket.recv(expected_size - len(chunks))
        if received == b"":
            raise RuntimeError("connection closed while reading MQTT packet")
        chunks.extend(received)
    return bytes(chunks)


def _decode_variable_byte_integer_from_bytes(data: bytes, start_index: int) -> tuple[int, int]:
    multiplier = 1
    value = 0
    consumed = 0
    index = start_index

    while True:
        if index >= len(data):
            raise ValueError("malformed variable byte integer")

        encoded_byte = data[index]
        consumed += 1
        value += (encoded_byte & 0x7F) * multiplier
        index += 1

        if (encoded_byte & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise ValueError("variable byte integer exceeds MQTT limit")

    return value, consumed


def _read_mqtt_packet(tcp_socket: socket.socket, timeout_seconds: float) -> bytes:
    tcp_socket.settimeout(timeout_seconds)
    fixed_header_first_byte = _read_exact_bytes(tcp_socket, 1)

    remaining_length_bytes = bytearray()
    while True:
        current = _read_exact_bytes(tcp_socket, 1)
        remaining_length_bytes.extend(current)
        if (current[0] & 0x80) == 0:
            break
        if len(remaining_length_bytes) >= 4:
            raise RuntimeError("invalid MQTT remaining length encoding")

    remaining_length, _ = _decode_variable_byte_integer_from_bytes(remaining_length_bytes, 0)
    payload = _read_exact_bytes(tcp_socket, remaining_length)
    return fixed_header_first_byte + bytes(remaining_length_bytes) + payload


def _mqtt_packet_type(packet: bytes) -> int:
    if not packet:
        raise ValueError("packet is empty")
    return packet[0] >> 4


def _mqtt_payload(packet: bytes) -> bytes:
    if len(packet) < 2:
        raise ValueError("packet is too short")

    remaining_length, consumed = _decode_variable_byte_integer_from_bytes(packet, 1)
    payload_start = 1 + consumed
    payload_end = payload_start + remaining_length
    if payload_end > len(packet):
        raise ValueError("packet remaining length exceeds available bytes")
    return packet[payload_start:payload_end]


def _decode_connack_reason_code(packet: bytes) -> int:
    if _mqtt_packet_type(packet) != 2:
        raise AssertionError(f"expected CONNACK packet type 2, got {_mqtt_packet_type(packet)}")
    payload = _mqtt_payload(packet)
    if len(payload) < 2:
        raise AssertionError("CONNACK payload too short")
    return int(payload[1])


def _decode_connack_server_keep_alive(packet: bytes) -> int | None:
    if _mqtt_packet_type(packet) != 2:
        raise AssertionError(f"expected CONNACK packet type 2, got {_mqtt_packet_type(packet)}")

    payload = _mqtt_payload(packet)
    if len(payload) < 3:
        raise AssertionError("CONNACK payload too short for properties")

    properties_length, consumed = _decode_variable_byte_integer_from_bytes(payload, 2)
    properties_start = 2 + consumed
    properties_end = properties_start + properties_length
    if properties_end > len(payload):
        raise ValueError("CONNACK properties length exceeds payload")

    properties = payload[properties_start:properties_end]
    index = 0

    while index < len(properties):
        property_identifier = properties[index]
        index += 1

        if property_identifier == 0x21:
            index += 2
            continue
        if property_identifier == 0x24:
            index += 1
            continue
        if property_identifier == 0x25:
            index += 1
            continue
        if property_identifier == 0x27:
            index += 4
            continue
        if property_identifier == 0x22:
            index += 2
            continue
        if property_identifier == 0x28:
            index += 1
            continue
        if property_identifier == 0x29:
            index += 1
            continue
        if property_identifier == 0x2A:
            index += 1
            continue
        if property_identifier == 0x13:
            if index + 2 > len(properties):
                raise ValueError("malformed Server Keep Alive property")
            return int.from_bytes(properties[index:index + 2], byteorder="big")

        if property_identifier == 0x1A:
            if index + 2 > len(properties):
                raise ValueError("malformed Response Information property")
            text_len = int.from_bytes(properties[index:index + 2], byteorder="big")
            index += 2 + text_len
            continue

        if property_identifier == 0x12:
            if index + 4 > len(properties):
                raise ValueError("malformed Assigned Client Identifier length")
            text_len = int.from_bytes(properties[index:index + 2], byteorder="big")
            index += 2 + text_len
            continue

        raise ValueError(f"unsupported CONNACK property identifier 0x{property_identifier:02X}")

    return None


def _connect_tcp(host: str, port: int, keep_alive_seconds: int, timeout_seconds: float) -> tuple[socket.socket, bytes]:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    tcp_socket.settimeout(timeout_seconds)

    payload = encode_utf8_string(_unique_client_id("keep-alive"))
    connect_packet = build_connect_packet(
        protocol_name="MQTT",
        protocol_version=5,
        connect_flags=0x02,
        keepalive_seconds=keep_alive_seconds,
        properties=b"",
        payload=payload,
    )
    tcp_socket.sendall(connect_packet)

    connack_packet = _read_mqtt_packet(tcp_socket, timeout_seconds)
    assert_reason_code(_decode_connack_reason_code(connack_packet), 0x00)
    return tcp_socket, connack_packet


def run_1_7_1_pingreq_pingresp(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket, _ = _connect_tcp(host, port, keep_alive_seconds=30, timeout_seconds=config.timeout_seconds)
        try:
            tcp_socket.sendall(b"\xC0\x00")
            pingresp = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            if _mqtt_packet_type(pingresp) != 13:
                return False, f"expected PINGRESP packet type 13, got {_mqtt_packet_type(pingresp)}"
        finally:
            tcp_socket.close()

        return True, "1.7.1 broker responds PINGRESP to PINGREQ"
    except Exception as error:
        return False, f"1.7.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_7_2_silent_client_exceeds_keep_alive_is_closed(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket, _ = _connect_tcp(host, port, keep_alive_seconds=1, timeout_seconds=config.timeout_seconds)
        try:
            deadline = time.monotonic() + 3.0
            disconnected = False

            while time.monotonic() < deadline:
                try:
                    packet = _read_mqtt_packet(tcp_socket, 0.5)
                    if _mqtt_packet_type(packet) == 14:
                        disconnected = True
                        break
                except TimeoutError:
                    continue
                except socket.timeout:
                    continue
                except RuntimeError as runtime_error:
                    if "connection closed" in str(runtime_error):
                        disconnected = True
                        break
                    raise

            if not disconnected:
                return False, "broker did not disconnect silent client after keep alive timeout"
        finally:
            tcp_socket.close()

        return True, "1.7.2 silent client disconnected after 1.5x keep alive"
    except Exception as error:
        return False, f"1.7.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_7_3_keep_alive_zero_disables_timeout(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket, _ = _connect_tcp(host, port, keep_alive_seconds=0, timeout_seconds=config.timeout_seconds)
        try:
            time.sleep(2.2)
            tcp_socket.sendall(b"\xC0\x00")
            pingresp = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            if _mqtt_packet_type(pingresp) != 13:
                return False, f"expected PINGRESP packet type 13, got {_mqtt_packet_type(pingresp)}"
        finally:
            tcp_socket.close()

        return True, "1.7.3 keep alive 0 kept connection alive without timeout"
    except Exception as error:
        return False, f"1.7.3 failed: {error}"
    finally:
        stop_broker(process)


def run_1_7_4_server_keep_alive_override_applied(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.server_keep_alive": 1,
            }
        )

        tcp_socket, connack = _connect_tcp(host, port, keep_alive_seconds=60, timeout_seconds=config.timeout_seconds)
        try:
            server_keep_alive = _decode_connack_server_keep_alive(connack)
            if server_keep_alive != 1:
                return False, (
                    "expected CONNACK Server Keep Alive = 1 "
                    f"but got {server_keep_alive}"
                )

            deadline = time.monotonic() + 3.0
            disconnected = False

            while time.monotonic() < deadline:
                try:
                    packet = _read_mqtt_packet(tcp_socket, 0.5)
                    if _mqtt_packet_type(packet) == 14:
                        disconnected = True
                        break
                except TimeoutError:
                    continue
                except socket.timeout:
                    continue
                except RuntimeError as runtime_error:
                    if "connection closed" in str(runtime_error):
                        disconnected = True
                        break
                    raise

            if not disconnected:
                return False, "broker did not enforce Server Keep Alive override timeout"
        finally:
            tcp_socket.close()

        return True, "1.7.4 Server Keep Alive override in CONNACK was enforced"
    except Exception as error:
        return False, f"1.7.4 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/keep_alive/pingreq_pingresp",
        "description": "1.7.1 Client sends PINGREQ and broker responds with PINGRESP",
        "run": run_1_7_1_pingreq_pingresp,
    },
    {
        "name": "connect/keep_alive/silent_client_timeout_disconnect",
        "description": "1.7.2 Silent client beyond 1.5x Keep Alive is disconnected",
        "run": run_1_7_2_silent_client_exceeds_keep_alive_is_closed,
    },
    {
        "name": "connect/keep_alive/keep_alive_zero_no_timeout",
        "description": "1.7.3 Keep Alive = 0 disables timeout",
        "run": run_1_7_3_keep_alive_zero_disables_timeout,
    },
    {
        "name": "connect/keep_alive/server_keep_alive_override_applied",
        "description": "1.7.4 Server Keep Alive override in CONNACK is enforced",
        "run": run_1_7_4_server_keep_alive_override_applied,
    },
]
