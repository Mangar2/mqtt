"""Integration tests for connection lifecycle section 1.2 (CONNECT properties)."""

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
build_packet = _raw_tcp_module.build_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for CONNECT property tests")
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
    return f"integration/connect/properties/{prefix}/{uuid.uuid4().hex}"


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


def _connack_property(connack_result, property_name: str):
    properties = getattr(connack_result, "properties", None)
    if properties is None:
        return None
    return getattr(properties, property_name, None)


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


def _read_mqtt_packet(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[int, bytes]:
    tcp_socket.settimeout(timeout_seconds)
    first_byte = _read_exact_bytes(tcp_socket, 1)[0]

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
    packet_type = first_byte >> 4
    return packet_type, payload


def _decode_connack_reason_code(payload: bytes) -> int:
    if len(payload) < 2:
        raise RuntimeError("invalid CONNACK payload")
    return int(payload[1])


def _decode_suback_reason_code(payload: bytes) -> int:
    if len(payload) < 3:
        raise RuntimeError("invalid SUBACK payload")
    properties_length, consumed = _decode_variable_byte_integer_from_bytes(payload, 2)
    reason_start = 2 + consumed + properties_length
    if reason_start >= len(payload):
        raise RuntimeError("SUBACK contains no reason code")
    return int(payload[reason_start])


def _decode_publish_qos1(payload: bytes) -> tuple[str, int, bytes]:
    if len(payload) < 5:
        raise RuntimeError("invalid QoS1 PUBLISH payload")
    topic_length = int.from_bytes(payload[0:2], byteorder="big")
    topic_start = 2
    topic_end = topic_start + topic_length
    if topic_end + 2 > len(payload):
        raise RuntimeError("truncated QoS1 PUBLISH topic or packet identifier")

    topic = payload[topic_start:topic_end].decode("utf-8")
    packet_identifier = int.from_bytes(payload[topic_end:topic_end + 2], byteorder="big")

    properties_length, consumed = _decode_variable_byte_integer_from_bytes(payload, topic_end + 2)
    body_start = topic_end + 2 + consumed + properties_length
    if body_start > len(payload):
        raise RuntimeError("truncated QoS1 PUBLISH properties")

    return topic, packet_identifier, payload[body_start:]


def _queued_message_roundtrip(
    host: str,
    port: int,
    timeout_seconds: float,
    client_id: str,
    connect_properties,
    topic: str,
    payload: bytes,
) -> tuple[bool, str]:
    with MqttClient(timeout_seconds=timeout_seconds) as subscriber_online:
        first_connack = subscriber_online.connect(
            host,
            port,
            client_id=client_id,
            clean_start=True,
            properties=connect_properties,
        )
        assert_connack(first_connack, reason_code=0x00, session_present=False)
        suback_codes = subscriber_online.subscribe(topic, qos=1)
        if not suback_codes:
            return False, "SUBACK for durable subscription is empty"
        assert_reason_code(suback_codes[0], 0x01)

    with MqttClient(timeout_seconds=timeout_seconds) as publisher:
        pub_connack = publisher.connect(
            host,
            port,
            client_id=_unique_client_id("prop-pub"),
            clean_start=True,
        )
        assert_connack(pub_connack, reason_code=0x00, session_present=False)
        publish_reason = publisher.publish(topic, payload, qos=1)
        assert_reason_code(publish_reason, 0x00)

    with MqttClient(timeout_seconds=timeout_seconds) as subscriber_resume:
        second_connack = subscriber_resume.connect(
            host,
            port,
            client_id=client_id,
            clean_start=False,
        )
        if not second_connack.session_present:
            return False, "expected Session Present = 1 on reconnect"

        messages = subscriber_resume.collect_messages(count=1, timeout=timeout_seconds)
        assert_message(messages[0], topic=topic, payload=payload, qos=1, retain=False)

    return True, "queued message delivered after reconnect"


def run_1_2_1_session_expiry_zero_discards_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("exp0")
    topic = _unique_topic("exp0")

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(SessionExpiryInterval=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber_online:
            first_connack = subscriber_online.connect(
                host,
                port,
                client_id=client_id,
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(first_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber_online.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for session-expiry=0 subscription is empty"
            assert_reason_code(suback_codes[0], 0x01)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("exp0-pub"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"should-not-queue", qos=1)
            if int(publish_reason) not in (0x00, 0x10):
                return (
                    False,
                    "expected PUBACK reason 0x00 or 0x10 for setup publish, "
                    f"got 0x{int(publish_reason):02X}",
                )

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber_resume:
            second_connack = subscriber_resume.connect(
                host,
                port,
                client_id=client_id,
                clean_start=False,
            )
            assert_connack(second_connack, reason_code=0x00, session_present=False)
            assert_no_message(subscriber_resume, timeout=min(1.5, config.timeout_seconds))

        return True, "1.2.1 session discarded when Session Expiry Interval = 0"
    except Exception as error:
        return False, f"1.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_2_session_expiry_positive_persists_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("exp-pos")
    topic = _unique_topic("exp-pos")

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(SessionExpiryInterval=120)
        success, detail = _queued_message_roundtrip(
            host=host,
            port=port,
            timeout_seconds=config.timeout_seconds,
            client_id=client_id,
            connect_properties=connect_props,
            topic=topic,
            payload=b"queued-exp-pos",
        )
        if not success:
            return False, f"1.2.2 failed: {detail}"
        return True, "1.2.2 session persisted when Session Expiry Interval > 0"
    except Exception as error:
        return False, f"1.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_3_session_expiry_max_never_expires(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("exp-max")
    topic = _unique_topic("exp-max")

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(SessionExpiryInterval=0xFFFFFFFF)
        success, detail = _queued_message_roundtrip(
            host=host,
            port=port,
            timeout_seconds=config.timeout_seconds,
            client_id=client_id,
            connect_properties=connect_props,
            topic=topic,
            payload=b"queued-exp-max",
        )
        if not success:
            return False, f"1.2.3 failed: {detail}"
        return True, "1.2.3 session persisted with Session Expiry Interval = 0xFFFFFFFF"
    except Exception as error:
        return False, f"1.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_4_receive_maximum_respected(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("receive-maximum")
    subscriber_socket: socket.socket | None = None
    publisher_payloads = (b"m1", b"m2")

    try:
        host, port, process = _start_isolated_broker()
        subscriber_socket = socket.create_connection((host, port), timeout=config.timeout_seconds)
        receive_maximum_property = b"\x21\x00\x01"  # property id 0x21 + uint16(1)
        connect_packet = build_connect_packet(
            keepalive_seconds=60,
            properties=receive_maximum_property,
            payload=encode_utf8_string(_unique_client_id("recvmax-sub")),
        )
        subscriber_socket.sendall(connect_packet)
        packet_type, payload = _read_mqtt_packet(subscriber_socket, config.timeout_seconds)
        if packet_type != 2:
            return False, f"expected CONNACK packet type 2, got {packet_type}"
        assert_reason_code(_decode_connack_reason_code(payload), 0x00)

        subscribe_packet = build_subscribe_packet([(topic, 1)], packet_identifier=7)
        subscriber_socket.sendall(subscribe_packet)
        packet_type, payload = _read_mqtt_packet(subscriber_socket, config.timeout_seconds)
        if packet_type != 9:
            return False, f"expected SUBACK packet type 9, got {packet_type}"
        assert_reason_code(_decode_suback_reason_code(payload), 0x01)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("recvmax-pub"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            assert_reason_code(publisher.publish(topic, publisher_payloads[0], qos=1), 0x00)
            assert_reason_code(publisher.publish(topic, publisher_payloads[1], qos=1), 0x00)

        packet_type, payload = _read_mqtt_packet(subscriber_socket, config.timeout_seconds)
        if packet_type != 3:
            return False, f"expected first broker packet to subscriber as PUBLISH (3), got {packet_type}"
        first_topic, first_packet_identifier, first_payload = _decode_publish_qos1(payload)
        if first_topic != topic or first_payload != publisher_payloads[0]:
            return False, "first queued QoS1 message mismatch while Receive Maximum=1"

        try:
            _read_mqtt_packet(subscriber_socket, min(0.7, max(0.2, config.timeout_seconds / 4)))
            return False, "broker delivered a second QoS1 message before PUBACK with Receive Maximum=1"
        except socket.timeout:
            pass

        puback_first = build_packet(
            packet_type=4,
            flags=0,
            variable_header=first_packet_identifier.to_bytes(2, byteorder="big"),
        )
        subscriber_socket.sendall(puback_first)

        packet_type, payload = _read_mqtt_packet(subscriber_socket, config.timeout_seconds)
        if packet_type != 3:
            return False, f"expected second broker packet as PUBLISH (3), got {packet_type}"
        second_topic, second_packet_identifier, second_payload = _decode_publish_qos1(payload)
        if second_topic != topic or second_payload != publisher_payloads[1]:
            return False, "second queued QoS1 message mismatch after PUBACK release"

        puback_second = build_packet(
            packet_type=4,
            flags=0,
            variable_header=second_packet_identifier.to_bytes(2, byteorder="big"),
        )
        subscriber_socket.sendall(puback_second)

        return True, "1.2.4 broker withheld second QoS1 delivery until first PUBACK when Receive Maximum=1"
    except Exception as error:
        return False, f"1.2.4 failed: {error}"
    finally:
        if subscriber_socket is not None:
            try:
                subscriber_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_1_2_5_maximum_packet_size_enforced(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("max-packet")
    oversized_payload = ("X" * 600).encode("utf-8")

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(MaximumPacketSize=80)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("maxpkt-sub"),
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "SUBACK for Maximum Packet Size test is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("maxpkt-pub"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, oversized_payload, qos=0), 0x00)

            disconnect_seen = False
            try:
                disconnect_event = subscriber.wait_for_disconnect(timeout=min(1.5, config.timeout_seconds))
                disconnect_seen = True
                if disconnect_event.reason_code not in (0x00, 0x95):
                    return False, (
                        "broker disconnected client with unexpected reason code "
                        f"0x{int(disconnect_event.reason_code):02X}"
                    )
            except TimeoutError:
                pass

            if not disconnect_seen:
                assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "1.2.5 broker did not deliver packet above declared Maximum Packet Size"
    except Exception as error:
        return False, f"1.2.5 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_6_topic_alias_maximum_respected(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("topic-alias")

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(TopicAliasMaximum=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("alias-sub"),
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "SUBACK for Topic Alias Maximum test is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("alias-pub"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"alias-check", qos=0), 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"alias-check", qos=0, retain=False)
            inbound_alias = getattr(messages[0].properties, "TopicAlias", None)
            if inbound_alias not in (None, 0):
                return False, f"expected no inbound topic alias, got {inbound_alias}"

        return True, "1.2.6 Topic Alias Maximum=0 respected (no inbound alias used)"
    except Exception as error:
        return False, f"1.2.6 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_7_request_problem_information_zero(config) -> tuple[bool, str]:
    process = None

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(RequestProblemInformation=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("problem-info"),
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            reason_string = _connack_property(connack, "ReasonString")
            if reason_string not in (None, ""):
                return False, f"expected no CONNACK ReasonString, got {reason_string!r}"

            user_property = _connack_property(connack, "UserProperty")
            if user_property not in (None, [], ()):
                return False, f"expected no CONNACK UserProperty, got {user_property!r}"

        return True, "1.2.7 Request Problem Information=0 omitted non-error diagnostics"
    except Exception as error:
        return False, f"1.2.7 failed: {error}"
    finally:
        stop_broker(process)


def run_1_2_8_request_response_information_one(config) -> tuple[bool, str]:
    process = None

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(RequestResponseInformation=1)

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("response-info"),
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            response_information = _connack_property(connack, "ResponseInformation")
            if not isinstance(response_information, str) or not response_information:
                return False, (
                    "expected non-empty CONNACK ResponseInformation when "
                    "Request Response Information=1"
                )

        return True, "1.2.8 CONNACK included Response Information"
    except Exception as error:
        return False, f"1.2.8 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/properties/session_expiry_zero_discards_session",
        "description": "1.2.1 Session Expiry Interval = 0 discards session",
        "run": run_1_2_1_session_expiry_zero_discards_session,
    },
    {
        "name": "connect/properties/session_expiry_positive_persists_session",
        "description": "1.2.2 Session Expiry Interval > 0 persists session",
        "run": run_1_2_2_session_expiry_positive_persists_session,
    },
    {
        "name": "connect/properties/session_expiry_max_never_expires",
        "description": "1.2.3 Session Expiry Interval = 0xFFFFFFFF persists session",
        "run": run_1_2_3_session_expiry_max_never_expires,
    },
    {
        "name": "connect/properties/receive_maximum_respected",
        "description": "1.2.4 Broker respects client Receive Maximum constraint",
        "run": run_1_2_4_receive_maximum_respected,
    },
    {
        "name": "connect/properties/maximum_packet_size_enforced",
        "description": "1.2.5 Broker does not send packet above Maximum Packet Size",
        "run": run_1_2_5_maximum_packet_size_enforced,
    },
    {
        "name": "connect/properties/topic_alias_maximum_respected",
        "description": "1.2.6 Broker respects client Topic Alias Maximum",
        "run": run_1_2_6_topic_alias_maximum_respected,
    },
    {
        "name": "connect/properties/request_problem_information_zero",
        "description": "1.2.7 Request Problem Information = 0 omits diagnostics on success",
        "run": run_1_2_7_request_problem_information_zero,
    },
    {
        "name": "connect/properties/request_response_information_one",
        "description": "1.2.8 Request Response Information = 1 includes Response Information",
        "run": run_1_2_8_request_response_information_one,
    },
]
