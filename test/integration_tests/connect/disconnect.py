"""Integration tests for connection lifecycle section 1.6 (DISCONNECT)."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import socket
import uuid

try:
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties
except Exception:
    PacketTypes = None
    Properties = None


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
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
encode_variable_byte_integer = _raw_tcp_module.encode_variable_byte_integer


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for DISCONNECT tests")
    return Properties, PacketTypes


def _new_connect_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.CONNECT)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _new_disconnect_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.DISCONNECT)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/connect/disconnect/{prefix}/{uuid.uuid4().hex}"


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


def _decode_utf8_field(data: bytes, start_index: int) -> tuple[str, int]:
    if start_index + 2 > len(data):
        raise ValueError("malformed UTF-8 field length")
    length = int.from_bytes(data[start_index:start_index + 2], byteorder="big")
    content_start = start_index + 2
    content_end = content_start + length
    if content_end > len(data):
        raise ValueError("malformed UTF-8 field bytes")
    return data[content_start:content_end].decode("utf-8"), content_end


def _decode_disconnect_reason_and_reason_string(packet: bytes) -> tuple[int, str | None]:
    if _mqtt_packet_type(packet) != 14:
        raise AssertionError(f"expected DISCONNECT packet type 14, got {_mqtt_packet_type(packet)}")

    payload = _mqtt_payload(packet)
    if len(payload) == 0:
        return 0x00, None

    reason_code = int(payload[0])
    if len(payload) == 1:
        return reason_code, None

    properties_length, consumed = _decode_variable_byte_integer_from_bytes(payload, 1)
    properties_start = 1 + consumed
    properties_end = properties_start + properties_length
    if properties_end > len(payload):
        raise ValueError("DISCONNECT properties length exceeds payload")

    properties = payload[properties_start:properties_end]
    reason_string = None
    index = 0

    while index < len(properties):
        property_identifier = properties[index]
        index += 1

        if property_identifier == 0x11:
            index += 4
            continue

        if property_identifier == 0x1F:
            reason_string, index = _decode_utf8_field(properties, index)
            continue

        if property_identifier == 0x1C:
            _, index = _decode_utf8_field(properties, index)
            continue

        if property_identifier == 0x26:
            _, index = _decode_utf8_field(properties, index)
            _, index = _decode_utf8_field(properties, index)
            continue

        raise ValueError(f"unsupported DISCONNECT property identifier 0x{property_identifier:02X}")

    return reason_code, reason_string


def _build_raw_connect_packet(
    *,
    client_id: str,
    clean_start: bool,
    session_expiry_interval: int | None = None,
    request_problem_information: int | None = None,
) -> bytes:
    connect_properties = bytearray()
    if session_expiry_interval is not None:
        connect_properties.extend(b"\x11")
        connect_properties.extend(int(session_expiry_interval).to_bytes(4, byteorder="big"))
    if request_problem_information is not None:
        connect_properties.extend(b"\x17")
        connect_properties.extend(bytes([int(request_problem_information) & 0xFF]))

    connect_flags = 0x02 if clean_start else 0x00
    payload = encode_utf8_string(client_id)
    return build_connect_packet(
        protocol_name="MQTT",
        protocol_version=5,
        connect_flags=connect_flags,
        keepalive_seconds=60,
        properties=bytes(connect_properties),
        payload=payload,
    )


def _build_disconnect_packet(*, reason_code: int, session_expiry_interval: int | None = None) -> bytes:
    properties = bytearray()
    if session_expiry_interval is not None:
        properties.extend(b"\x11")
        properties.extend(int(session_expiry_interval).to_bytes(4, byteorder="big"))

    variable_header = bytearray()
    variable_header.append(int(reason_code) & 0xFF)
    variable_header.extend(encode_variable_byte_integer(len(properties)))
    variable_header.extend(properties)

    fixed_header = bytes([0xE0]) + encode_variable_byte_integer(len(variable_header))
    return fixed_header + bytes(variable_header)


def run_1_6_1_disconnect_0x00_clean_close_will_not_published(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("disconnect-00")
    payload = b"will-should-not-be-published"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("disc00-sub"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for will observer is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as will_client:
                will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
                will_connack = will_client.connect(
                    host,
                    port,
                    client_id=_unique_client_id("disc00-pub"),
                    clean_start=True,
                )
                assert_connack(will_connack, reason_code=0x00, session_present=False)
                will_client.disconnect(reason_code=0x00)

            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "1.6.1 DISCONNECT 0x00 closed cleanly and suppressed will publication"
    except Exception as error:
        return False, f"1.6.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_6_2_disconnect_0x04_will_is_published(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("disconnect-04")
    payload = b"will-must-be-published"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("disc04-sub"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for will observer is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as will_client:
                will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
                will_connack = will_client.connect(
                    host,
                    port,
                    client_id=_unique_client_id("disc04-pub"),
                    clean_start=True,
                )
                assert_connack(will_connack, reason_code=0x00, session_present=False)
                will_client.disconnect(reason_code=0x04)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=1, retain=False)

        return True, "1.6.2 DISCONNECT 0x04 triggered will publication"
    except Exception as error:
        return False, f"1.6.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_6_3_disconnect_session_expiry_override_is_applied(config) -> tuple[bool, str]:
    process = None
    durable_client_id = _unique_client_id("disc-expiry")
    topic = _unique_topic("expiry-override")

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connect_props = _new_connect_properties(SessionExpiryInterval=120)
            first_connack = subscriber.connect(
                host,
                port,
                client_id=durable_client_id,
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(first_connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for durable session setup is empty"
            assert_reason_code(suback_codes[0], 0x01)

            disconnect_props = _new_disconnect_properties(SessionExpiryInterval=0)
            subscriber.disconnect(reason_code=0x00, properties=disconnect_props)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("disc-expiry-pub"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"should-not-be-queued", qos=1)
            if int(publish_reason) not in (0x00, 0x10):
                return (
                    False,
                    "expected PUBACK reason 0x00 or 0x10 for setup publish, "
                    f"got 0x{int(publish_reason):02X}",
                )

        with MqttClient(timeout_seconds=config.timeout_seconds) as resume_client:
            resume_connack = resume_client.connect(
                host,
                port,
                client_id=durable_client_id,
                clean_start=False,
            )
            assert_connack(resume_connack, reason_code=0x00, session_present=False)
            assert_no_message(resume_client, timeout=min(1.5, config.timeout_seconds))

        return True, "1.6.3 DISCONNECT session-expiry override was applied"
    except Exception as error:
        return False, f"1.6.3 failed: {error}"
    finally:
        stop_broker(process)


def run_1_6_4_disconnect_cannot_increase_expiry_from_zero(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("disc-expiry-protocol-error")

    try:
        host, port, process = _start_isolated_broker()

        connect_packet = _build_raw_connect_packet(
            client_id=client_id,
            clean_start=True,
            session_expiry_interval=0,
        )
        invalid_disconnect_packet = _build_disconnect_packet(
            reason_code=0x00,
            session_expiry_interval=60,
        )

        with socket.create_connection((host, port), timeout=config.timeout_seconds) as tcp_socket:
            tcp_socket.settimeout(config.timeout_seconds)
            tcp_socket.sendall(connect_packet)
            connack_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            connack_reason = _decode_connack_reason_code(connack_packet)
            assert_reason_code(connack_reason, 0x00)

            tcp_socket.sendall(invalid_disconnect_packet)
            disconnect_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            disconnect_reason, _ = _decode_disconnect_reason_and_reason_string(disconnect_packet)
            assert_reason_code(disconnect_reason, 0x82)

        with MqttClient(timeout_seconds=config.timeout_seconds) as reconnect_client:
            reconnect_connack = reconnect_client.connect(
                host,
                port,
                client_id=client_id,
                clean_start=False,
            )
            assert_connack(reconnect_connack, reason_code=0x00, session_present=False)

        return True, "1.6.4 invalid DISCONNECT expiry increase was rejected with Protocol Error"
    except Exception as error:
        return False, f"1.6.4 failed: {error}"
    finally:
        stop_broker(process)


def run_1_6_5_server_disconnect_includes_reason_code_and_string(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("disc-server-reason")

    try:
        host, port, process = _start_isolated_broker()

        first_connect_packet = _build_raw_connect_packet(
            client_id=client_id,
            clean_start=True,
            request_problem_information=1,
        )
        second_connect_packet = _build_raw_connect_packet(
            client_id=_unique_client_id("second-connect-same-socket"),
            clean_start=True,
            request_problem_information=1,
        )

        with socket.create_connection((host, port), timeout=config.timeout_seconds) as tcp_socket:
            tcp_socket.settimeout(config.timeout_seconds)
            tcp_socket.sendall(first_connect_packet)
            connack_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            connack_reason = _decode_connack_reason_code(connack_packet)
            assert_reason_code(connack_reason, 0x00)

            tcp_socket.sendall(second_connect_packet)
            disconnect_packet = _read_mqtt_packet(tcp_socket, config.timeout_seconds)
            disconnect_reason, reason_string = _decode_disconnect_reason_and_reason_string(disconnect_packet)

            if disconnect_reason == 0x00:
                return False, "expected server-initiated DISCONNECT with non-success reason code"
            if reason_string is None or reason_string.strip() == "":
                return False, "expected server-initiated DISCONNECT to include a non-empty Reason String"

        return (
            True,
            "1.6.5 server-initiated DISCONNECT included reason code "
            f"0x{disconnect_reason:02X} and reason string {reason_string!r}",
        )
    except Exception as error:
        return False, f"1.6.5 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/disconnect/client_disconnect_00_clean_close_will_not_published",
        "description": "1.6.1 Client DISCONNECT 0x00 closes cleanly and suppresses will publication",
        "run": run_1_6_1_disconnect_0x00_clean_close_will_not_published,
    },
    {
        "name": "connect/disconnect/client_disconnect_04_publishes_will",
        "description": "1.6.2 Client DISCONNECT 0x04 triggers will publication",
        "run": run_1_6_2_disconnect_0x04_will_is_published,
    },
    {
        "name": "connect/disconnect/disconnect_session_expiry_override_applied",
        "description": "1.6.3 DISCONNECT Session Expiry Interval override is applied",
        "run": run_1_6_3_disconnect_session_expiry_override_is_applied,
    },
    {
        "name": "connect/disconnect/disconnect_cannot_increase_expiry_from_zero",
        "description": "1.6.4 DISCONNECT cannot increase Session Expiry from 0 to non-zero (Protocol Error)",
        "run": run_1_6_4_disconnect_cannot_increase_expiry_from_zero,
    },
    {
        "name": "connect/disconnect/server_disconnect_includes_reason_code_and_reason_string",
        "description": "1.6.5 Server-initiated DISCONNECT includes Reason Code and Reason String",
        "run": run_1_6_5_server_disconnect_includes_reason_code_and_string,
    },
]
