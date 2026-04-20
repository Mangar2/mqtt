"""Integration tests for message properties end-to-end section 12.1 to 12.5."""

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

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/message-properties/{prefix}/{uuid.uuid4().hex}"


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


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _new_publish_properties(**values):
    properties = Properties(PacketTypes.PUBLISH)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _message_property(message, property_name: str):
    properties = getattr(message, "properties", None)
    if properties is None:
        return None
    return getattr(properties, property_name, None)


def _connect_subscriber(host: str, port: int, timeout_seconds: float, topic: str, qos: int = 1) -> MqttClient:
    subscriber = MqttClient(timeout_seconds=timeout_seconds)
    connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-msg-props"), clean_start=True)
    assert_connack(connack, reason_code=0x00, session_present=False)
    suback_codes = subscriber.subscribe(topic, qos=qos)
    if not suback_codes:
        raise AssertionError("SUBACK is empty")
    assert_reason_code(suback_codes[0], qos)
    return subscriber


def _connect_publisher(host: str, port: int, timeout_seconds: float) -> MqttClient:
    publisher = MqttClient(timeout_seconds=timeout_seconds)
    connack = publisher.connect(host, port, client_id=_unique_client_id("pub-msg-props"), clean_start=True)
    assert_connack(connack, reason_code=0x00, session_present=False)
    return publisher


def _create_persistent_subscription(
    host: str,
    port: int,
    timeout_seconds: float,
    *,
    client_id: str,
    topic: str,
    session_expiry_seconds: int,
) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
        connack = subscriber.connect(
            host,
            port,
            client_id=client_id,
            clean_start=True,
            properties=_new_connect_properties(SessionExpiryInterval=session_expiry_seconds),
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        suback_codes = subscriber.subscribe(topic, qos=1)
        if not suback_codes:
            raise AssertionError("SUBACK is empty while creating persistent subscription")
        assert_reason_code(suback_codes[0], 1)


def run_12_1_1_payload_format_indicator_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-1-1")
        payload = b'{"kind":"utf8"}'

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(PayloadFormatIndicator=1)
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        payload_format = _message_property(message, "PayloadFormatIndicator")
        if int(payload_format) != 1:
            return False, f"expected PayloadFormatIndicator=1, got {payload_format!r}"

        return True, "12.1.1 payload format indicator forwarded"
    except Exception as error:
        return False, f"12.1.1 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_1_2_invalid_utf8_with_payload_format_indicator(config) -> tuple[bool, str]:
    process = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-1-2")

        publisher = _connect_publisher(host, port, config.timeout_seconds)
        publish_properties = _new_publish_properties(PayloadFormatIndicator=1)

        disconnected = False
        accepted = False
        try:
            publish_reason = publisher.publish(topic, b"\xff\xfe\xfa", qos=1, properties=publish_properties)
            accepted = int(publish_reason) in (0x00, 0x10)
        except Exception:
            disconnected = True

        if not disconnected:
            try:
                publisher.wait_for_disconnect(timeout=min(1.0, config.timeout_seconds))
                disconnected = True
            except Exception:
                disconnected = False

        with MqttClient(timeout_seconds=config.timeout_seconds) as probe:
            probe_connack = probe.connect(host, port, client_id=_unique_client_id("probe-12-1-2"), clean_start=True)
            assert_connack(probe_connack, reason_code=0x00, session_present=False)

        if disconnected:
            return True, "12.1.2 broker disconnected publisher on invalid UTF-8 payload"
        if accepted:
            return True, "12.1.2 broker accepted invalid UTF-8 payload (allowed by MAY behavior)"
        return False, "invalid UTF-8 payload was neither disconnected nor acknowledged"
    except Exception as error:
        return False, f"12.1.2 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_2_1_content_type_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-2-1")
        payload = b'{"content":"typed"}'
        expected_content_type = "application/json"

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(ContentType=expected_content_type)
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        actual_content_type = _message_property(message, "ContentType")
        if actual_content_type != expected_content_type:
            return False, (
                f"expected ContentType={expected_content_type!r}, got {actual_content_type!r}"
            )

        return True, "12.2.1 content type forwarded"
    except Exception as error:
        return False, f"12.2.1 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_3_1_response_topic_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-3-1")
        payload = b"request-response"
        expected_response_topic = _unique_topic("response")

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(ResponseTopic=expected_response_topic)
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        actual_response_topic = _message_property(message, "ResponseTopic")
        if actual_response_topic != expected_response_topic:
            return False, (
                f"expected ResponseTopic={expected_response_topic!r}, got {actual_response_topic!r}"
            )

        return True, "12.3.1 response topic forwarded"
    except Exception as error:
        return False, f"12.3.1 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_3_2_correlation_data_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-3-2")
        payload = b"request-with-correlation"
        expected_correlation_data = b"corr-12-3-2"

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(CorrelationData=expected_correlation_data)
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        actual_correlation_data = _message_property(message, "CorrelationData")
        if actual_correlation_data != expected_correlation_data:
            return False, (
                f"expected CorrelationData={expected_correlation_data!r}, got {actual_correlation_data!r}"
            )

        return True, "12.3.2 correlation data forwarded"
    except Exception as error:
        return False, f"12.3.2 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_3_3_request_response_pattern(config) -> tuple[bool, str]:
    process = None
    requester = None
    responder = None
    try:
        host, port, process = _start_isolated_broker()
        request_topic = _unique_topic("12-3-3-request")
        response_topic = _unique_topic("12-3-3-response")
        correlation_data = b"corr-12-3-3"

        requester = MqttClient(timeout_seconds=config.timeout_seconds)
        requester_connack = requester.connect(
            host,
            port,
            client_id=_unique_client_id("requester"),
            clean_start=True,
        )
        assert_connack(requester_connack, reason_code=0x00, session_present=False)
        requester_suback = requester.subscribe(response_topic, qos=1)
        if not requester_suback:
            return False, "requester SUBACK was empty"
        assert_reason_code(requester_suback[0], 1)

        responder = MqttClient(timeout_seconds=config.timeout_seconds)
        responder_connack = responder.connect(
            host,
            port,
            client_id=_unique_client_id("responder"),
            clean_start=True,
        )
        assert_connack(responder_connack, reason_code=0x00, session_present=False)
        responder_suback = responder.subscribe(request_topic, qos=1)
        if not responder_suback:
            return False, "responder SUBACK was empty"
        assert_reason_code(responder_suback[0], 1)

        request_properties = _new_publish_properties(
            ResponseTopic=response_topic,
            CorrelationData=correlation_data,
        )
        request_reason = requester.publish(
            request_topic,
            b"request",
            qos=1,
            properties=request_properties,
        )
        assert_reason_code(request_reason, 0x00)

        request_message = responder.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(request_message, topic=request_topic, payload=b"request", qos=1, retain=False)
        if _message_property(request_message, "ResponseTopic") != response_topic:
            return False, "responder did not receive request ResponseTopic"
        if _message_property(request_message, "CorrelationData") != correlation_data:
            return False, "responder did not receive request CorrelationData"

        response_properties = _new_publish_properties(CorrelationData=correlation_data)
        response_reason = responder.publish(
            response_topic,
            b"response",
            qos=1,
            properties=response_properties,
        )
        assert_reason_code(response_reason, 0x00)

        response_message = requester.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(response_message, topic=response_topic, payload=b"response", qos=1, retain=False)
        if _message_property(response_message, "CorrelationData") != correlation_data:
            return False, "requester response CorrelationData mismatch"

        return True, "12.3.3 request/response pattern works with ResponseTopic"
    except Exception as error:
        return False, f"12.3.3 failed: {error}"
    finally:
        if responder is not None:
            try:
                responder.disconnect()
            except Exception:
                pass
        if requester is not None:
            try:
                requester.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_4_1_message_expiry_decremented_in_forwarded_publish(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-4-1")
        subscriber_client_id = _unique_client_id("sub-12-4-1")
        original_expiry = 10

        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=subscriber_client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("pub-12-4-1"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            publish_properties = _new_publish_properties(MessageExpiryInterval=original_expiry)
            publish_reason = publisher.publish(topic, b"expiring", qos=1, properties=publish_properties)
            assert_reason_code(publish_reason, 0x00)

        time.sleep(2.0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed_subscriber:
            resumed_connack = resumed_subscriber.connect(
                host,
                port,
                client_id=subscriber_client_id,
                clean_start=False,
            )
            assert_connack(resumed_connack, reason_code=0x00, session_present=True)

            message = resumed_subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
            assert_message(message, topic=topic, payload=b"expiring", qos=1, retain=False)

            remaining_expiry = _message_property(message, "MessageExpiryInterval")
            if remaining_expiry is None:
                return False, "forwarded message had no MessageExpiryInterval"
            remaining_expiry_int = int(remaining_expiry)
            if remaining_expiry_int <= 0 or remaining_expiry_int >= original_expiry:
                return False, (
                    f"expected decremented expiry in range 1..{original_expiry - 1}, "
                    f"got {remaining_expiry_int}"
                )

        return True, "12.4.1 Message Expiry Interval decremented before delivery"
    except Exception as error:
        return False, f"12.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_12_4_2_expired_message_dropped_before_delivery(config) -> tuple[bool, str]:
    process = None
    publisher = None
    subscriber = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-4-2")

        publisher = _connect_publisher(host, port, config.timeout_seconds)
        publish_properties = _new_publish_properties(MessageExpiryInterval=1)
        publish_reason = publisher.publish(
            topic,
            b"retained-to-expire",
            qos=1,
            retain=True,
            properties=publish_properties,
        )
        if int(publish_reason) not in (0x00, 0x10):
            return False, (
                "unexpected PUBACK reason for retained publish without live subscribers: "
                f"expected 0x00 or 0x10, got 0x{int(publish_reason):02X}"
            )

        time.sleep(2.0)

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        assert_no_message(subscriber, timeout=min(2.0, config.timeout_seconds))

        return True, "12.4.2 expired retained message dropped before delivery"
    except Exception as error:
        return False, f"12.4.2 failed: {error}"
    finally:
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_4_3_offline_queue_removes_expired_messages(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-4-3")
        subscriber_client_id = _unique_client_id("sub-12-4-3")

        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=subscriber_client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            publisher_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("pub-12-4-3"),
                clean_start=True,
            )
            assert_connack(publisher_connack, reason_code=0x00, session_present=False)
            publish_properties = _new_publish_properties(MessageExpiryInterval=1)
            publish_reason = publisher.publish(
                topic,
                b"offline-expire",
                qos=1,
                properties=publish_properties,
            )
            assert_reason_code(publish_reason, 0x00)

        time.sleep(2.0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed_subscriber:
            resumed_connack = resumed_subscriber.connect(
                host,
                port,
                client_id=subscriber_client_id,
                clean_start=False,
            )
            assert_connack(resumed_connack, reason_code=0x00, session_present=True)
            assert_no_message(resumed_subscriber, timeout=min(2.0, config.timeout_seconds))

        return True, "12.4.3 expired offline-queued messages removed before reconnect delivery"
    except Exception as error:
        return False, f"12.4.3 failed: {error}"
    finally:
        stop_broker(process)


def run_12_5_1_user_properties_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-5-1")
        payload = b"single-user-property"
        expected_property = ("origin", "integration-suite")

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(UserProperty=[expected_property])
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        user_properties = _message_property(message, "UserProperty")
        if user_properties is None or expected_property not in user_properties:
            return False, f"expected UserProperty {expected_property!r}, got {user_properties!r}"

        return True, "12.5.1 user property forwarded"
    except Exception as error:
        return False, f"12.5.1 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_12_5_2_multiple_user_properties_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber = None
    publisher = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("12-5-2")
        payload = b"multiple-user-properties"
        expected_properties = [
            ("trace-id", "abc123"),
            ("tenant", "north"),
            ("tenant", "south"),
        ]

        subscriber = _connect_subscriber(host, port, config.timeout_seconds, topic, qos=1)
        publisher = _connect_publisher(host, port, config.timeout_seconds)

        publish_properties = _new_publish_properties(UserProperty=expected_properties)
        publish_reason = publisher.publish(topic, payload, qos=1, properties=publish_properties)
        assert_reason_code(publish_reason, 0x00)

        message = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        actual_properties = _message_property(message, "UserProperty")
        if actual_properties is None:
            return False, "expected UserProperty list, got none"
        for expected_pair in expected_properties:
            if expected_pair not in actual_properties:
                return False, (
                    f"expected UserProperty pair {expected_pair!r} in {actual_properties!r}"
                )

        return True, "12.5.2 multiple user properties forwarded"
    except Exception as error:
        return False, f"12.5.2 failed: {error}"
    finally:
        if publisher is not None:
            try:
                publisher.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


TEST_CASES = [
    {
        "name": "message_properties/payload_format_indicator_forwarded",
        "description": "12.1.1 Publish Payload Format Indicator=1 is forwarded",
        "run": run_12_1_1_payload_format_indicator_forwarded,
    },
    {
        "name": "message_properties/invalid_utf8_payload_may_disconnect",
        "description": "12.1.2 Invalid UTF-8 payload with indicator=1 may disconnect",
        "run": run_12_1_2_invalid_utf8_with_payload_format_indicator,
    },
    {
        "name": "message_properties/content_type_forwarded",
        "description": "12.2.1 Publish Content Type is forwarded",
        "run": run_12_2_1_content_type_forwarded,
    },
    {
        "name": "message_properties/response_topic_forwarded",
        "description": "12.3.1 Publish Response Topic is forwarded",
        "run": run_12_3_1_response_topic_forwarded,
    },
    {
        "name": "message_properties/correlation_data_forwarded",
        "description": "12.3.2 Publish Correlation Data is forwarded",
        "run": run_12_3_2_correlation_data_forwarded,
    },
    {
        "name": "message_properties/request_response_pattern",
        "description": "12.3.3 Request/Response pattern with Response Topic works",
        "run": run_12_3_3_request_response_pattern,
    },
    {
        "name": "message_properties/message_expiry_decremented",
        "description": "12.4.1 Message Expiry Interval decremented in forwarded PUBLISH",
        "run": run_12_4_1_message_expiry_decremented_in_forwarded_publish,
    },
    {
        "name": "message_properties/expired_message_dropped",
        "description": "12.4.2 Message expired before delivery is dropped",
        "run": run_12_4_2_expired_message_dropped_before_delivery,
    },
    {
        "name": "message_properties/offline_queue_expired_messages_removed",
        "description": "12.4.3 Offline queue removes expired messages before reconnect delivery",
        "run": run_12_4_3_offline_queue_removes_expired_messages,
    },
    {
        "name": "message_properties/user_property_forwarded",
        "description": "12.5.1 Publish User Property is forwarded",
        "run": run_12_5_1_user_properties_forwarded,
    },
    {
        "name": "message_properties/multiple_user_properties_forwarded",
        "description": "12.5.2 Multiple User Properties are all forwarded",
        "run": run_12_5_2_multiple_user_properties_forwarded,
    },
]