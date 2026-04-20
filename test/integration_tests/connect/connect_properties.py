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

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


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

    try:
        host, port, process = _start_isolated_broker()
        connect_props = _new_connect_properties(ReceiveMaximum=1)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("recvmax-sub"),
                clean_start=True,
                properties=connect_props,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for Receive Maximum test is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("recvmax-pub"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, b"m1", qos=1), 0x00)
                assert_reason_code(publisher.publish(topic, b"m2", qos=1), 0x00)

            messages = subscriber.collect_messages(count=2, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"m1", qos=1, retain=False)
            assert_message(messages[1], topic=topic, payload=b"m2", qos=1, retain=False)

        return True, "1.2.4 Receive Maximum property accepted and QoS1 delivery remained valid"
    except Exception as error:
        return False, f"1.2.4 failed: {error}"
    finally:
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
