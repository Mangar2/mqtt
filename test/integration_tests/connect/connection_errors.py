"""Integration tests for connection lifecycle section 1.8 (Connection Errors)."""

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
_mqtt_client_module = _load_helper("mqtt_client")
_raw_tcp_module = _load_helper("raw_tcp")

assert_connection_closed = _assertions_module.assert_connection_closed
assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/connect/connection-errors/{prefix}/{uuid.uuid4().hex}"


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


def _decode_disconnect_reason_code(packet: bytes) -> int:
    if _mqtt_packet_type(packet) != 14:
        raise AssertionError(f"expected DISCONNECT packet type 14, got {_mqtt_packet_type(packet)}")
    payload = _mqtt_payload(packet)
    if len(payload) == 0:
        return 0x00
    return int(payload[0])


def _expect_socket_closed(tcp_socket: socket.socket, timeout_seconds: float) -> bool:
    tcp_socket.settimeout(0.2)
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            probe = tcp_socket.recv(1)
        except socket.timeout:
            continue
        if probe == b"":
            return True
    return False


def _open_raw_connected_socket(host: str, port: int, timeout_seconds: float) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    tcp_socket.settimeout(timeout_seconds)

    connect_packet = build_connect_packet(
        protocol_name="MQTT",
        protocol_version=5,
        connect_flags=0x02,
        keepalive_seconds=30,
        properties=b"",
        payload=encode_utf8_string(_unique_client_id("conn-errors-raw")),
    )
    tcp_socket.sendall(connect_packet)
    connack_packet = _read_mqtt_packet(tcp_socket, timeout_seconds)
    assert_reason_code(_decode_connack_reason_code(connack_packet), 0x00)
    return tcp_socket


def _force_abrupt_close(client: MqttClient) -> None:
    raw_client = getattr(client, "_client", None)
    if raw_client is None:
        raise RuntimeError("internal paho client is unavailable")

    client_socket = raw_client.socket()
    if client_socket is None:
        raise RuntimeError("client socket is unavailable")

    client_socket.close()
    raw_client.loop_stop()


def run_1_8_1_first_packet_not_connect_connection_closed(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        assert_connection_closed(host, port, b"\xC0\x00", timeout=config.timeout_seconds)
        return True, "1.8.1 broker closed connection when first packet was not CONNECT"
    except Exception as error:
        return False, f"1.8.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_8_2_second_connect_same_connection_protocol_error_82(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _open_raw_connected_socket(host, port, config.timeout_seconds)

        second_connect = build_connect_packet(
            protocol_name="MQTT",
            protocol_version=5,
            connect_flags=0x02,
            keepalive_seconds=30,
            properties=b"",
            payload=encode_utf8_string(_unique_client_id("conn-errors-second-connect")),
        )
        tcp_socket.sendall(second_connect)

        disconnect_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
        reason_code = _decode_disconnect_reason_code(disconnect_packet)
        assert_reason_code(reason_code, 0x82)

        if not _expect_socket_closed(tcp_socket, timeout_seconds=config.timeout_seconds):
            return False, "broker did not close socket after second CONNECT protocol error"

        return True, "1.8.2 second CONNECT returned DISCONNECT 0x82 and closed the connection"
    except Exception as error:
        return False, f"1.8.2 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_1_8_3_malformed_packet_disconnect_81_and_close(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _open_raw_connected_socket(host, port, config.timeout_seconds)

        # PUBLISH packet with impossible topic length (remaining length is 2 but topic length says 5).
        malformed_publish = b"\x30\x02\x00\x05"
        tcp_socket.sendall(malformed_publish)

        disconnect_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
        reason_code = _decode_disconnect_reason_code(disconnect_packet)
        assert_reason_code(reason_code, 0x81)

        if not _expect_socket_closed(tcp_socket, timeout_seconds=config.timeout_seconds):
            return False, "broker did not close socket after malformed packet"

        return True, "1.8.3 malformed packet returned DISCONNECT 0x81 and closed the connection"
    except Exception as error:
        return False, f"1.8.3 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_1_8_4_abrupt_tcp_close_detected(config) -> tuple[bool, str]:
    process = None
    abrupt_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        will_topic = _unique_topic("abrupt-close")

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            subscriber_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("conn-errors-sub"),
                clean_start=True,
            )
            assert_connack(subscriber_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(will_topic, qos=1)
            if not suback_codes:
                return False, "missing SUBACK for abrupt close observer"
            assert_reason_code(suback_codes[0], 0x01)

            abrupt_client = MqttClient(timeout_seconds=config.timeout_seconds)
            abrupt_client.set_will(topic=will_topic, payload=b"abrupt-close", qos=1, retain=False)
            abrupt_connack = abrupt_client.connect(
                host,
                port,
                client_id=_unique_client_id("conn-errors-abrupt"),
                clean_start=True,
                keepalive=30,
            )
            assert_connack(abrupt_connack, reason_code=0x00, session_present=False)

            _force_abrupt_close(abrupt_client)

            detection_timeout = min(3.0, max(1.0, config.timeout_seconds))
            message = subscriber.collect_messages(count=1, timeout=detection_timeout)[0]
            assert_message(
                message,
                topic=will_topic,
                payload=b"abrupt-close",
                qos=1,
                retain=False,
            )

        return True, "1.8.4 abrupt TCP close detected independently from keep-alive timeout"
    except Exception as error:
        return False, f"1.8.4 failed: {error}"
    finally:
        if abrupt_client is not None:
            try:
                abrupt_client.disconnect()
            except Exception:
                pass
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/connection_errors/first_packet_not_connect_connection_closed",
        "description": "1.8.1 First packet is not CONNECT closes connection",
        "run": run_1_8_1_first_packet_not_connect_connection_closed,
    },
    {
        "name": "connect/connection_errors/second_connect_same_connection_protocol_error_82",
        "description": "1.8.2 Second CONNECT packet on same connection returns Protocol Error 0x82",
        "run": run_1_8_2_second_connect_same_connection_protocol_error_82,
    },
    {
        "name": "connect/connection_errors/malformed_packet_disconnect_81_and_close",
        "description": "1.8.3 Malformed packet returns DISCONNECT 0x81 and closes connection",
        "run": run_1_8_3_malformed_packet_disconnect_81_and_close,
    },
    {
        "name": "connect/connection_errors/abrupt_tcp_close_detected",
        "description": "1.8.4 Abrupt TCP close is detected by broker",
        "run": run_1_8_4_abrupt_tcp_close_detected,
    },
]
