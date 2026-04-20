"""Integration tests for flow control section 9.1 to 9.2."""

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
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
build_publish_packet = _raw_tcp_module.build_publish_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for flow control tests")
    return Properties, PacketTypes


def _new_connect_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.CONNECT)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/flow/control/{prefix}/{uuid.uuid4().hex}"


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


def _encode_variable_byte_integer(value: int) -> bytes:
    if value < 0:
        raise ValueError("value must be non-negative")
    encoded = bytearray()
    remaining = value
    while True:
        current = remaining % 128
        remaining //= 128
        if remaining > 0:
            current |= 0x80
        encoded.append(current)
        if remaining == 0:
            break
    return bytes(encoded)


def _encode_connect_properties(*, receive_maximum: int | None = None, maximum_packet_size: int | None = None) -> bytes:
    encoded = bytearray()
    if receive_maximum is not None:
        encoded.append(0x21)
        encoded.extend(int(receive_maximum).to_bytes(2, byteorder="big"))
    if maximum_packet_size is not None:
        encoded.append(0x27)
        encoded.extend(int(maximum_packet_size).to_bytes(4, byteorder="big"))
    return bytes(encoded)


def _recv_exact(tcp_socket: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = tcp_socket.recv(count - len(data))
        if not chunk:
            raise RuntimeError("socket closed while reading packet")
        data.extend(chunk)
    return bytes(data)


def _recv_packet(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[int, int, bytes, int]:
    tcp_socket.settimeout(timeout_seconds)
    first_byte = _recv_exact(tcp_socket, 1)[0]

    remaining_length = 0
    multiplier = 1
    encoded_length_bytes = 0
    while True:
        encoded = _recv_exact(tcp_socket, 1)[0]
        encoded_length_bytes += 1
        remaining_length += (encoded & 0x7F) * multiplier
        if (encoded & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise RuntimeError("malformed remaining length")

    payload = _recv_exact(tcp_socket, remaining_length)
    packet_type = (first_byte >> 4) & 0x0F
    packet_flags = first_byte & 0x0F
    total_size = 1 + encoded_length_bytes + remaining_length
    return packet_type, packet_flags, payload, total_size


def _parse_connack_reason(payload: bytes) -> int:
    if len(payload) < 2:
        raise RuntimeError("invalid CONNACK payload")
    return int(payload[1])


def _parse_disconnect_reason(payload: bytes) -> int:
    if not payload:
        return 0x00
    return int(payload[0])


def _parse_publish_packet_identifier(packet_flags: int, payload: bytes) -> int:
    qos = (packet_flags >> 1) & 0x03
    if qos == 0:
        return 0
    if len(payload) < 2:
        raise RuntimeError("PUBLISH payload too short for topic length")
    topic_length = int.from_bytes(payload[0:2], byteorder="big")
    packet_id_index = 2 + topic_length
    if len(payload) < packet_id_index + 2:
        raise RuntimeError("PUBLISH payload too short for packet identifier")
    return int.from_bytes(payload[packet_id_index:packet_id_index + 2], byteorder="big")


def _build_puback(packet_identifier: int, reason_code: int = 0x00) -> bytes:
    variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([reason_code]) + b"\x00"
    return bytes([0x40, len(variable_header)]) + variable_header


def _build_pingreq() -> bytes:
    return b"\xC0\x00"


def _build_pubrec(packet_identifier: int, reason_code: int = 0x00) -> bytes:
    variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([reason_code]) + b"\x00"
    return bytes([0x50, len(variable_header)]) + variable_header


def _connect_raw_client(
    host: str,
    port: int,
    timeout_seconds: float,
    client_id: str,
    *,
    receive_maximum: int | None = None,
    maximum_packet_size: int | None = None,
) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_properties = _encode_connect_properties(
            receive_maximum=receive_maximum,
            maximum_packet_size=maximum_packet_size,
        )
        connect_packet = build_connect_packet(
            properties=connect_properties,
            payload=encode_utf8_string(client_id),
        )
        tcp_socket.sendall(connect_packet)
        packet_type, _packet_flags, payload, _packet_size = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 2:
            raise RuntimeError(f"expected CONNACK packet type 2, got {packet_type}")
        reason_code = _parse_connack_reason(payload)
        if reason_code != 0x00:
            raise RuntimeError(f"CONNECT rejected with reason 0x{reason_code:02X}")
        return tcp_socket
    except Exception:
        tcp_socket.close()
        raise


def _subscribe_raw_qos1(tcp_socket: socket.socket, topic: str, timeout_seconds: float) -> None:
    subscribe_packet = build_subscribe_packet(
        topic_filters=[(topic, 1)],
        packet_identifier=1,
        properties=b"",
    )
    tcp_socket.sendall(subscribe_packet)
    packet_type, _packet_flags, payload, _packet_size = _recv_packet(tcp_socket, timeout_seconds)
    if packet_type != 9:
        raise RuntimeError(f"expected SUBACK packet type 9, got {packet_type}")
    if len(payload) < 3:
        raise RuntimeError("invalid SUBACK payload")
    reason_code = int(payload[-1])
    if reason_code not in (0x00, 0x01):
        raise RuntimeError(f"SUBACK rejected subscription with reason 0x{reason_code:02X}")


def _read_broker_maximum_packet_size(host: str, port: int, timeout_seconds: float) -> int | None:
    with MqttClient(timeout_seconds=timeout_seconds) as client:
        connack = client.connect(
            host,
            port,
            client_id=_unique_client_id("maxpkt-probe"),
            clean_start=True,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        properties = getattr(connack, "properties", None)
        if properties is None:
            return None
        value = getattr(properties, "MaximumPacketSize", None)
        if value is None:
            return None
        return int(value)


def _build_oversized_publish_for_limit(topic: str, limit: int) -> bytes:
    if limit < 16:
        raise RuntimeError(f"broker Maximum Packet Size is unexpectedly small: {limit}")

    payload_length = max(1, limit)
    max_payload_length = 1024 * 1024
    while payload_length <= max_payload_length:
        packet = build_publish_packet(topic=topic, payload=b"X" * payload_length, qos=0)
        if len(packet) > limit:
            return packet
        payload_length += 1

    raise RuntimeError(
        "unable to construct oversized packet within safety bounds; "
        f"broker limit may be too large ({limit})"
    )


def run_9_1_1_broker_respects_client_receive_maximum(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-1-1")

    try:
        host, port, process = _start_isolated_broker()
        subscriber = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("sub-9-1-1"),
            receive_maximum=1,
        )
        try:
            _subscribe_raw_qos1(subscriber, topic, config.timeout_seconds)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-9-1-1"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"m1", qos=1), 0x00)
                assert_reason_code(publisher.publish(topic, b"m2", qos=1), 0x00)

            packet_type, packet_flags, payload, _packet_size = _recv_packet(subscriber, config.timeout_seconds)
            if packet_type != 3:
                return False, f"expected first inbound PUBLISH (type 3), got packet type {packet_type}"

            first_packet_id = _parse_publish_packet_identifier(packet_flags, payload)
            if first_packet_id <= 0:
                return False, "expected QoS1/QoS2 inbound message with non-zero packet identifier"

            subscriber.settimeout(min(1.0, max(0.2, config.timeout_seconds / 4)))
            try:
                second_packet_type, _flags, _payload, _size = _recv_packet(subscriber, subscriber.gettimeout())
            except socket.timeout:
                subscriber.sendall(_build_puback(first_packet_id))
                return True, "9.1.1 broker respected Receive Maximum=1 (no second unacknowledged publish sent)"

            return (
                False,
                "9.1.1 expected broker to hold back second QoS1 publish until ACK, "
                f"but received packet type {second_packet_type}",
            )
        finally:
            try:
                subscriber.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_9_1_2_client_exceeds_broker_receive_maximum(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-1-2")

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.receive_maximum": 1,
            }
        )

        publisher = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("pub-9-1-2"),
        )
        try:
            first_publish = build_publish_packet(topic=topic, payload=b"first", qos=2, packet_identifier=11)
            second_publish = build_publish_packet(topic=topic, payload=b"second", qos=2, packet_identifier=12)
            publisher.sendall(first_publish)

            packet_type, _packet_flags, payload, _packet_size = _recv_packet(publisher, config.timeout_seconds)
            if packet_type != 5:
                return False, f"9.1.2 expected PUBREC (type 5) for first QoS2 publish, got type {packet_type}"
            if _parse_disconnect_reason(payload) != 0x00:
                return False, "9.1.2 first QoS2 publish was not accepted with PUBREC success"

            publisher.sendall(second_publish)

            publisher.settimeout(min(2.0, max(0.5, config.timeout_seconds / 2)))
            while True:
                packet_type, _packet_flags, payload, _packet_size = _recv_packet(publisher, publisher.gettimeout())
                if packet_type == 14:
                    disconnect_reason = _parse_disconnect_reason(payload)
                    if disconnect_reason != 0x93:
                        return (
                            False,
                            "9.1.2 expected DISCONNECT 0x93 when exceeding broker Receive Maximum, "
                            f"got 0x{disconnect_reason:02X}",
                        )
                    return True, "9.1.2 broker rejected excess inflight QoS with DISCONNECT 0x93"
                if packet_type == 5:
                    return False, (
                        "9.1.2 broker sent PUBREC for second QoS2 publish instead of enforcing "
                        "Receive Maximum with DISCONNECT 0x93"
                    )
        except socket.timeout:
            return False, "9.1.2 expected DISCONNECT 0x93 but broker kept connection open"
        finally:
            try:
                publisher.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_9_1_3_broker_resumes_after_ack(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-1-3")

    try:
        host, port, process = _start_isolated_broker()
        subscriber = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("sub-9-1-3"),
            receive_maximum=1,
        )
        try:
            _subscribe_raw_qos1(subscriber, topic, config.timeout_seconds)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-9-1-3"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"first", qos=1), 0x00)
                assert_reason_code(publisher.publish(topic, b"second", qos=1), 0x00)

            packet_type, packet_flags, payload, _packet_size = _recv_packet(subscriber, config.timeout_seconds)
            if packet_type != 3:
                return False, f"expected first inbound PUBLISH (type 3), got packet type {packet_type}"
            first_packet_id = _parse_publish_packet_identifier(packet_flags, payload)
            if first_packet_id <= 0:
                return False, "expected first inbound message with valid packet identifier"

            subscriber.sendall(_build_puback(first_packet_id))

            packet_type, packet_flags, payload, _packet_size = _recv_packet(subscriber, config.timeout_seconds)
            if packet_type != 3:
                return False, f"expected second inbound PUBLISH after ACK, got packet type {packet_type}"
            second_packet_id = _parse_publish_packet_identifier(packet_flags, payload)
            if second_packet_id <= 0 or second_packet_id == first_packet_id:
                return False, "expected second inbound message with a new packet identifier"

            subscriber.sendall(_build_puback(second_packet_id))
            return True, "9.1.3 broker resumed sending after PUBACK freed inflight slot"
        finally:
            try:
                subscriber.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_9_2_1_client_maximum_packet_size_respected(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-2-1")
    client_maximum_packet_size = 90

    try:
        host, port, process = _start_isolated_broker()
        subscriber = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("sub-9-2-1"),
            maximum_packet_size=client_maximum_packet_size,
        )
        try:
            _subscribe_raw_qos1(subscriber, topic, config.timeout_seconds)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-9-2-1"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"small", qos=0), 0x00)

            packet_type, _packet_flags, payload, packet_size = _recv_packet(subscriber, config.timeout_seconds)
            if packet_type != 3:
                return False, f"expected inbound PUBLISH for small message, got packet type {packet_type}"
            if packet_size > client_maximum_packet_size:
                return (
                    False,
                    "9.2.1 broker sent packet larger than client's Maximum Packet Size: "
                    f"{packet_size} > {client_maximum_packet_size}",
                )

            topic_name_length = int.from_bytes(payload[0:2], byteorder="big")
            topic_name = payload[2:2 + topic_name_length].decode("utf-8")
            message_payload = payload[2 + topic_name_length + 1:]
            if topic_name != topic or message_payload != b"small":
                return False, "9.2.1 small message payload/topic mismatch"

            return True, "9.2.1 broker respected client Maximum Packet Size on outbound delivery"
        finally:
            try:
                subscriber.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_9_2_2_client_packet_exceeds_broker_maximum(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-2-2")

    try:
        host, port, process = _start_isolated_broker()
        broker_limit = _read_broker_maximum_packet_size(host, port, config.timeout_seconds)
        if broker_limit is None:
            return False, "9.2.2 broker CONNACK did not include MaximumPacketSize"

        if broker_limit >= 268435455:
            return True, (
                "9.2.2 broker advertises protocol maximum packet size (268435455); "
                "no larger MQTT packet can be constructed to validate DISCONNECT 0x95"
            )

        oversized_publish = _build_oversized_publish_for_limit(topic, broker_limit)

        publisher = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("pub-9-2-2"),
        )
        try:
            publisher.sendall(oversized_publish)
            publisher.settimeout(min(2.0, max(0.5, config.timeout_seconds / 2)))
            while True:
                packet_type, _packet_flags, payload, _packet_size = _recv_packet(publisher, publisher.gettimeout())
                if packet_type == 14:
                    disconnect_reason = _parse_disconnect_reason(payload)
                    if disconnect_reason != 0x95:
                        return (
                            False,
                            "9.2.2 expected DISCONNECT 0x95 for oversized inbound packet, "
                            f"got 0x{disconnect_reason:02X}",
                        )
                    return True, "9.2.2 broker rejected oversized inbound packet with DISCONNECT 0x95"
        except socket.timeout:
            return False, "9.2.2 expected DISCONNECT 0x95 but broker kept connection open"
        finally:
            try:
                publisher.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_9_2_3_too_large_message_dropped_for_limited_subscriber(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("9-2-3")
    limited_maximum_packet_size = 90
    large_payload = b"L" * 256

    try:
        host, port, process = _start_isolated_broker()

        limited_subscriber = _connect_raw_client(
            host,
            port,
            config.timeout_seconds,
            _unique_client_id("sub-limited-9-2-3"),
            maximum_packet_size=limited_maximum_packet_size,
        )
        try:
            _subscribe_raw_qos1(limited_subscriber, topic, config.timeout_seconds)

            with MqttClient(timeout_seconds=config.timeout_seconds) as regular_subscriber:
                sub_connack = regular_subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-regular-9-2-3"),
                    clean_start=True,
                )
                assert_connack(sub_connack, reason_code=0x00, session_present=False)
                suback_codes = regular_subscriber.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, "9.2.3 regular subscriber SUBACK is empty"
                assert_reason_code(suback_codes[0], 0x00)

                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                    pub_connack = publisher.connect(
                        host,
                        port,
                        client_id=_unique_client_id("pub-9-2-3"),
                        clean_start=True,
                    )
                    assert_connack(pub_connack, reason_code=0x00, session_present=False)
                    assert_reason_code(publisher.publish(topic, large_payload, qos=0), 0x00)

                messages = regular_subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
                assert_message(messages[0], topic=topic, payload=large_payload, qos=0, retain=False)

            limited_subscriber.settimeout(min(1.2, max(0.3, config.timeout_seconds / 3)))
            try:
                packet_type, _packet_flags, _payload, _packet_size = _recv_packet(
                    limited_subscriber,
                    limited_subscriber.gettimeout(),
                )
                if packet_type == 14:
                    return False, "9.2.3 limited subscriber was disconnected instead of silent drop"
                if packet_type == 3:
                    return False, "9.2.3 limited subscriber received oversized message unexpectedly"
                return False, f"9.2.3 expected no packet for limited subscriber, got packet type {packet_type}"
            except socket.timeout:
                limited_subscriber.sendall(_build_pingreq())
                packet_type, _packet_flags, _payload, _packet_size = _recv_packet(
                    limited_subscriber,
                    config.timeout_seconds,
                )
                if packet_type != 13:
                    return (
                        False,
                        "9.2.3 limited subscriber did not remain healthy after silent drop "
                        f"(expected PINGRESP type 13, got {packet_type})",
                    )

            return True, "9.2.3 oversized message was dropped only for limited subscriber"
        finally:
            try:
                limited_subscriber.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"9.2.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "flow/receive_maximum/broker_respects_client_receive_maximum",
        "description": "9.1.1 Broker does not send more unacknowledged QoS 1/2 than client's Receive Maximum",
        "run": run_9_1_1_broker_respects_client_receive_maximum,
    },
    {
        "name": "flow/receive_maximum/client_exceeds_broker_receive_maximum",
        "description": "9.1.2 Client exceeds broker's Receive Maximum -> Protocol Error 0x93",
        "run": run_9_1_2_client_exceeds_broker_receive_maximum,
    },
    {
        "name": "flow/receive_maximum/broker_resumes_after_ack",
        "description": "9.1.3 After ACK received, broker resumes sending",
        "run": run_9_1_3_broker_resumes_after_ack,
    },
    {
        "name": "flow/maximum_packet_size/client_limit_never_exceeded",
        "description": "9.2.1 Client sets Maximum Packet Size -> broker never sends packet exceeding it",
        "run": run_9_2_1_client_maximum_packet_size_respected,
    },
    {
        "name": "flow/maximum_packet_size/client_packet_exceeds_broker_limit",
        "description": "9.2.2 Client sends packet exceeding broker's Maximum Packet Size -> DISCONNECT 0x95",
        "run": run_9_2_2_client_packet_exceeds_broker_maximum,
    },
    {
        "name": "flow/maximum_packet_size/message_dropped_for_limited_subscriber",
        "description": "9.2.3 Message too large for subscriber's limit -> message silently dropped for that subscriber",
        "run": run_9_2_3_too_large_message_dropped_for_limited_subscriber,
    },
]