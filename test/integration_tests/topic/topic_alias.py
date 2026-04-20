"""Integration tests for Topic Alias behavior (8.1 - 8.2)."""

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

assert_connack = _assertions_module.assert_connack
assert_disconnected = _assertions_module.assert_disconnected
assert_message = _assertions_module.assert_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for topic alias tests")
    return Properties, PacketTypes


def _new_connect_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.CONNECT)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _new_publish_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.PUBLISH)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _connack_property(connack_result, property_name: str):
    properties = getattr(connack_result, "properties", None)
    if properties is None:
        return None
    return getattr(properties, property_name, None)


def _message_topic_alias(message) -> int | None:
    properties = getattr(message, "properties", None)
    if properties is None:
        return None
    alias = getattr(properties, "TopicAlias", None)
    if alias is None:
        return None
    return int(alias)


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/topic/alias/{prefix}/{uuid.uuid4().hex}"


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


def run_8_1_1_client_publish_with_topic_and_alias_creates_mapping(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("8-1-1")
    payload = b"inbound-map-create"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-8-1-1"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "8.1.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-8-1-1"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publisher.enable_topic_alias(maximum=8)
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "8.1.1 client publish with topic+alias accepted and routed"
    except Exception as error:
        return False, f"8.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_8_1_2_client_publish_alias_only_resolves_stored_mapping(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("8-1-2")
    payload_first = b"map-seed"
    payload_second = b"map-resolve"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-8-1-2"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "8.1.2 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-8-1-2"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publisher.enable_topic_alias(maximum=8)

                assert_reason_code(publisher.publish(topic, payload_first, qos=0), 0x00)
                assert_reason_code(publisher.publish(topic, payload_second, qos=0), 0x00)

            messages = subscriber.collect_messages(count=2, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload_first, qos=0, retain=False)
            assert_message(messages[1], topic=topic, payload=payload_second, qos=0, retain=False)

        return True, "8.1.2 alias-only publish resolved previously stored mapping"
    except Exception as error:
        return False, f"8.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_8_1_3_client_alias_above_maximum_protocol_error(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("8-1-3")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("pub-8-1-3"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            alias_maximum = _connack_property(connack, "TopicAliasMaximum")
            maximum_value = int(alias_maximum) if alias_maximum is not None else 0
            invalid_alias = max(1, maximum_value + 1)

            publish_properties = _new_publish_properties(TopicAlias=invalid_alias)
            assert_reason_code(
                publisher.publish(topic, b"above-max", qos=0, properties=publish_properties),
                0x00,
            )
            assert_disconnected(publisher, reason_code=0x82, timeout=min(2.0, config.timeout_seconds))

        return True, "8.1.3 alias above Topic Alias Maximum rejected with Protocol Error"
    except Exception as error:
        return False, f"8.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_8_1_4_client_alias_without_mapping_protocol_error(config) -> tuple[bool, str]:
    process = None

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("pub-8-1-4"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            publish_properties = _new_publish_properties(TopicAlias=1)
            assert_reason_code(
                publisher.publish("", b"missing-map", qos=0, properties=publish_properties),
                0x00,
            )
            assert_disconnected(publisher, reason_code=0x82, timeout=min(2.0, config.timeout_seconds))

        return True, "8.1.4 alias-only publish without mapping rejected with Protocol Error"
    except Exception as error:
        return False, f"8.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_8_2_1_broker_uses_alias_when_client_allows(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("8-2-1")

    try:
        host, port, process = _start_isolated_broker()
        connect_properties = _new_connect_properties(TopicAliasMaximum=4)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-8-2-1"),
                clean_start=True,
                properties=connect_properties,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "8.2.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-8-2-1"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"outbound-alias-1", qos=0), 0x00)
                assert_reason_code(publisher.publish(topic, b"outbound-alias-2", qos=0), 0x00)

            messages = subscriber.collect_messages(count=2, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"outbound-alias-1", qos=0, retain=False)
            assert_message(messages[1], topic=topic, payload=b"outbound-alias-2", qos=0, retain=False)

            aliases = [_message_topic_alias(message) for message in messages]
            invalid_aliases = [alias for alias in aliases if alias is not None and (alias <= 0 or alias > 4)]
            if invalid_aliases:
                return False, f"8.2.1 broker sent invalid Topic Alias values {invalid_aliases} for maximum 4"

        if any(alias is not None for alias in aliases):
            return True, "8.2.1 broker sent valid Topic Alias values within client maximum"
        return True, "8.2.1 broker sent no Topic Alias (allowed), payload delivery still valid"
    except Exception as error:
        return False, f"8.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_8_2_2_broker_does_not_exceed_client_maximum(config) -> tuple[bool, str]:
    process = None
    topic_a = _unique_topic("8-2-2-a")
    topic_b = _unique_topic("8-2-2-b")

    try:
        host, port, process = _start_isolated_broker()
        connect_properties = _new_connect_properties(TopicAliasMaximum=1)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-8-2-2"),
                clean_start=True,
                properties=connect_properties,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic_a, qos=0)
            if not suback_codes:
                return False, "8.2.2 SUBACK for topic_a is empty"
            assert_reason_code(suback_codes[0], 0x00)

            suback_codes = subscriber.subscribe(topic_b, qos=0)
            if not suback_codes:
                return False, "8.2.2 SUBACK for topic_b is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-8-2-2"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic_a, b"a1", qos=0), 0x00)
                assert_reason_code(publisher.publish(topic_b, b"b1", qos=0), 0x00)
                assert_reason_code(publisher.publish(topic_a, b"a2", qos=0), 0x00)
                assert_reason_code(publisher.publish(topic_b, b"b2", qos=0), 0x00)

            messages = subscriber.collect_messages(count=4, timeout=config.timeout_seconds)
            expected = {
                (topic_a, b"a1"),
                (topic_b, b"b1"),
                (topic_a, b"a2"),
                (topic_b, b"b2"),
            }
            seen = {(message.topic, message.payload) for message in messages}
            if seen != expected:
                return False, f"8.2.2 expected payload set {expected}, got {seen}"

            aliases = [_message_topic_alias(message) for message in messages]
            exceeding_aliases = [alias for alias in aliases if alias is not None and alias > 1]
            if exceeding_aliases:
                return False, f"8.2.2 broker used alias above maximum 1: {exceeding_aliases}"

        return True, "8.2.2 broker never exceeded client Topic Alias Maximum"
    except Exception as error:
        return False, f"8.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_8_2_3_client_maximum_zero_broker_never_sends_alias(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("8-2-3")

    try:
        host, port, process = _start_isolated_broker()
        connect_properties = _new_connect_properties(TopicAliasMaximum=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-8-2-3"),
                clean_start=True,
                properties=connect_properties,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "8.2.3 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-8-2-3"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"no-alias-1", qos=0), 0x00)
                assert_reason_code(publisher.publish(topic, b"no-alias-2", qos=0), 0x00)

            messages = subscriber.collect_messages(count=2, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"no-alias-1", qos=0, retain=False)
            assert_message(messages[1], topic=topic, payload=b"no-alias-2", qos=0, retain=False)

            aliases = [_message_topic_alias(message) for message in messages]
            non_zero_aliases = [alias for alias in aliases if alias not in (None, 0)]
            if non_zero_aliases:
                return False, f"8.2.3 expected no Topic Alias from broker, got {non_zero_aliases}"

        return True, "8.2.3 client TopicAliasMaximum=0 prevented outbound Topic Alias"
    except Exception as error:
        return False, f"8.2.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "topic/alias/inbound_topic_plus_alias_creates_mapping",
        "description": "8.1.1 Client sends PUBLISH with Topic + Alias -> broker creates mapping",
        "run": run_8_1_1_client_publish_with_topic_and_alias_creates_mapping,
    },
    {
        "name": "topic/alias/inbound_alias_only_resolves_mapping",
        "description": "8.1.2 Client sends PUBLISH with Alias only (empty topic) -> broker resolves mapping",
        "run": run_8_1_2_client_publish_alias_only_resolves_stored_mapping,
    },
    {
        "name": "topic/alias/inbound_alias_above_maximum_protocol_error",
        "description": "8.1.3 Client sends Alias above Topic Alias Maximum -> Protocol Error",
        "run": run_8_1_3_client_alias_above_maximum_protocol_error,
    },
    {
        "name": "topic/alias/inbound_alias_without_mapping_protocol_error",
        "description": "8.1.4 Client sends Alias without prior mapping and empty topic -> Protocol Error",
        "run": run_8_1_4_client_alias_without_mapping_protocol_error,
    },
    {
        "name": "topic/alias/outbound_uses_alias_when_allowed",
        "description": "8.2.1 Broker uses Topic Alias when client Topic Alias Maximum > 0",
        "run": run_8_2_1_broker_uses_alias_when_client_allows,
    },
    {
        "name": "topic/alias/outbound_does_not_exceed_client_maximum",
        "description": "8.2.2 Broker does not exceed client Topic Alias Maximum",
        "run": run_8_2_2_broker_does_not_exceed_client_maximum,
    },
    {
        "name": "topic/alias/outbound_maximum_zero_no_alias_sent",
        "description": "8.2.3 Client Topic Alias Maximum = 0 -> broker never sends Topic Alias",
        "run": run_8_2_3_client_maximum_zero_broker_never_sends_alias,
    },
]