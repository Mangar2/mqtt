"""Integration tests for robustness section 19.3 (Resource Exhaustion)."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import socket
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
assert_message = _assertions_module.assert_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties
build_connect_packet = _raw_tcp_module.build_connect_packet
build_publish_packet = _raw_tcp_module.build_publish_packet
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
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _verify_valid_connect(host: str, port: int, timeout_seconds: float) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as client:
        connack = client.connect(
            host,
            port,
            client_id=_unique_client_id("robustness-19-3-verify"),
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


def run_19_3_1_extremely_deep_topic_subscription_handled(config) -> tuple[bool, str]:
    process = None
    deep_topic = "/".join(f"lvl-{level}" for level in range(100))
    payload = b"19.3.1-deep-topic"

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))
        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-3-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(deep_topic, qos=1)
            if not suback_codes:
                return False, "19.3.1 SUBACK was empty for extremely deep topic filter"

            suback_reason = int(suback_codes[0])
            if suback_reason <= 0x02:
                with MqttClient(timeout_seconds=timeout_seconds) as publisher:
                    pub_connack = publisher.connect(
                        host, port, client_id=_unique_client_id("pub-19-3-1"), clean_start=True
                    )
                    assert_connack(pub_connack, reason_code=0x00, session_present=False)
                    assert_reason_code(publisher.publish(deep_topic, payload, qos=0), 0x00)
                messages = subscriber.collect_messages(count=1, timeout=max(timeout_seconds, 6.0))
                assert_message(messages[0], topic=deep_topic, payload=payload, qos=0, retain=False)
            elif suback_reason < 0x80:
                return False, f"19.3.1 unexpected SUBACK reason code 0x{suback_reason:02X}"

        _verify_valid_connect(host, port, timeout_seconds)
        return True, "19.3.1 deep topic filter handled safely (accepted or explicitly rejected) without instability"
    except Exception as error:
        return False, f"19.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_19_3_2_maximum_topic_length_publish_handled(config) -> tuple[bool, str]:
    process = None
    _prefix = "19.3.2/"
    topic = _prefix + "t" * (65535 - len(_prefix))
    subscribe_filter = "19.3.2/#"
    payload = b"19.3.2-max-topic-length"

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-3-2"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(subscribe_filter, qos=1)
            if not suback_codes:
                return False, "19.3.2 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as publisher:
                pub_connack = publisher.connect(
                    host, port, client_id=_unique_client_id("pub-19-3-2"), clean_start=True
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = int(publisher.publish(topic, payload, qos=1))

            if publish_reason == 0x00:
                messages = subscriber.collect_messages(count=1, timeout=max(timeout_seconds, 12.0))
                assert_message(messages[0], topic=topic, payload=payload, qos=1, retain=False)
            elif publish_reason < 0x80:
                return False, f"19.3.2 unexpected PUBACK reason code 0x{publish_reason:02X}"

        _verify_valid_connect(host, port, timeout_seconds)
        return True, "19.3.2 maximum topic length publish was handled explicitly without broker instability"
    except Exception as error:
        return False, f"19.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_19_3_3_maximum_properties_publish_parsed(config) -> tuple[bool, str]:
    process = None
    topic = f"integration/robustness/19-3-3/{uuid.uuid4().hex}"
    payload = b"19.3.3-many-properties"
    user_property_count = 128

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-3-3"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "19.3.3 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as publisher:
                pub_connack = publisher.connect(
                    host, port, client_id=_unique_client_id("pub-19-3-3"), clean_start=True
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)

                publish_properties = Properties(PacketTypes.PUBLISH)
                setattr(
                    publish_properties,
                    "UserProperty",
                    [(f"key-{index}", f"value-{index}") for index in range(user_property_count)],
                )
                assert_reason_code(
                    publisher.publish(topic, payload, qos=1, properties=publish_properties),
                    0x00,
                )

            messages = subscriber.collect_messages(count=1, timeout=max(timeout_seconds, 8.0))
            message = messages[0]
            assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

            inbound_user_properties = getattr(getattr(message, "properties", None), "UserProperty", None)
            if not isinstance(inbound_user_properties, list):
                return False, "19.3.3 inbound publish had no parseable UserProperty list"
            if len(inbound_user_properties) != user_property_count:
                return (
                    False,
                    "19.3.3 user property count mismatch: "
                    f"expected={user_property_count}, got={len(inbound_user_properties)}",
                )

        _verify_valid_connect(host, port, timeout_seconds)
        return True, "19.3.3 publish with high property count parsed and delivered correctly"
    except Exception as error:
        return False, f"19.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_19_3_4_packet_identifier_exhaustion_handled_gracefully(config) -> tuple[bool, str]:
    process = None
    target_packets = 65535
    payload = b"x"
    topic = "integration/robustness/19-3-4/exhaustion"

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(20.0, float(config.timeout_seconds) * 2.0)

        connect_packet = build_connect_packet(payload=encode_utf8_string(_unique_client_id("raw-19-3-4")))
        with socket.create_connection((host, port), timeout=timeout_seconds) as tcp_socket:
            tcp_socket.settimeout(timeout_seconds)
            tcp_socket.sendall(connect_packet)
            packet_type, _packet_flags, connack_payload = _recv_mqtt_packet(tcp_socket)
            if packet_type != 2:
                return False, f"19.3.4 expected CONNACK packet type 2, got {packet_type}"
            if len(connack_payload) < 2 or int(connack_payload[1]) != 0x00:
                return False, f"19.3.4 expected CONNACK success reason 0x00, got payload={connack_payload!r}"

            for packet_identifier in range(1, target_packets + 1):
                tcp_socket.sendall(
                    build_publish_packet(
                        topic=topic,
                        payload=payload,
                        qos=1,
                        packet_identifier=packet_identifier,
                    )
                )

            tcp_socket.sendall(
                build_publish_packet(
                    topic=topic,
                    payload=payload,
                    qos=1,
                    packet_identifier=1,
                )
            )

            puback_count = 0
            closed_by_peer = False
            observation_deadline = socket.getdefaulttimeout()
            _ = observation_deadline
            tcp_socket.settimeout(0.5)
            for _iteration in range(40):
                try:
                    packet_type, _packet_flags, _payload_bytes = _recv_mqtt_packet(tcp_socket)
                except socket.timeout:
                    continue
                except RuntimeError:
                    closed_by_peer = True
                    break
                if packet_type == 4:
                    puback_count += 1
                elif packet_type == 14:
                    closed_by_peer = True
                    break

        if puback_count == 0 and not closed_by_peer:
            return (
                False,
                "19.3.4 broker neither acknowledged QoS1 packets nor disconnected on packet-id exhaustion pressure",
            )

        _verify_valid_connect(host, port, max(1.0, float(config.timeout_seconds)))
        return (
            True,
            "19.3.4 packet identifier exhaustion pressure handled gracefully "
            f"(pubacks={puback_count}, disconnected={closed_by_peer})",
        )
    except Exception as error:
        return False, f"19.3.4 failed: {error}"
    finally:
        stop_broker(process)


def run_19_3_5_rapid_topic_alias_creation_limit_enforced(config) -> tuple[bool, str]:
    process = None
    alias_limit = 10
    topic_root = f"integration/robustness/19-3-5/{uuid.uuid4().hex}"

    try:
        host, port, process = _start_isolated_broker({"broker.topic_alias_maximum": alias_limit})
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-3-5"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(f"{topic_root}/#", qos=0)
            if not suback_codes:
                return False, "19.3.5 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-19-3-5"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)

                for alias in range(1, alias_limit + 1):
                    topic = f"{topic_root}/alias-{alias}"
                    payload = f"mapped-{alias}".encode("utf-8")
                    publish_properties = Properties(PacketTypes.PUBLISH)
                    setattr(publish_properties, "TopicAlias", alias)
                    assert_reason_code(
                        publisher.publish(topic, payload, qos=0, properties=publish_properties),
                        0x00,
                    )

                delivered_messages = subscriber.collect_messages(count=alias_limit, timeout=max(timeout_seconds, 8.0))
                delivered_topics = {message.topic for message in delivered_messages}
                expected_topics = {f"{topic_root}/alias-{alias}" for alias in range(1, alias_limit + 1)}
                if delivered_topics != expected_topics:
                    missing = len(expected_topics - delivered_topics)
                    return False, f"19.3.5 alias mapping delivery mismatch, missing={missing}"

                invalid_alias_properties = Properties(PacketTypes.PUBLISH)
                setattr(invalid_alias_properties, "TopicAlias", alias_limit + 1)
                publisher.publish(
                    f"{topic_root}/alias-out-of-range",
                    b"invalid-alias",
                    qos=0,
                    properties=invalid_alias_properties,
                )
                disconnect_event = publisher.wait_for_disconnect(timeout=max(timeout_seconds, 6.0))
                if int(disconnect_event.reason_code) not in (0x94, 0x82):
                    return (
                        False,
                        "19.3.5 invalid alias did not trigger expected disconnect reason "
                        f"(got 0x{int(disconnect_event.reason_code):02X})",
                    )

        _verify_valid_connect(host, port, timeout_seconds)
        return True, "19.3.5 rapid topic alias creation worked up to limit and out-of-range alias was rejected"
    except Exception as error:
        return False, f"19.3.5 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "robustness/resource_exhaustion_deep_topic_subscription",
        "description": "19.3.1 Client subscribes to extremely deep topic (100 levels) -> handled or rejected, no crash",
        "run": run_19_3_1_extremely_deep_topic_subscription_handled,
    },
    {
        "name": "robustness/resource_exhaustion_max_topic_length",
        "description": "19.3.2 Client sends topic name at maximum length (65535 bytes) -> handled correctly",
        "run": run_19_3_2_maximum_topic_length_publish_handled,
    },
    {
        "name": "robustness/resource_exhaustion_max_properties",
        "description": "19.3.3 Client sends maximum number of properties -> parsed correctly",
        "run": run_19_3_3_maximum_properties_publish_parsed,
    },
    {
        "name": "robustness/resource_exhaustion_packet_identifier_space",
        "description": "19.3.4 Packet Identifier exhaustion (65535 inflight QoS 1 messages) -> broker handles gracefully",
        "run": run_19_3_4_packet_identifier_exhaustion_handled_gracefully,
    },
    {
        "name": "robustness/resource_exhaustion_topic_alias_limit",
        "description": "19.3.5 Rapid topic alias creation up to limit -> all tracked, limit enforced",
        "run": run_19_3_5_rapid_topic_alias_creation_limit_enforced,
    },
]
