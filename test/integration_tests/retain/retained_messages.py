"""Integration tests for Retained Messages (4.1 - 4.5)."""

from __future__ import annotations

import importlib.util
import socket
import time
import uuid
from pathlib import Path


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
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties
SubscribeOptions = _mqtt_client_module.SubscribeOptions


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _run_prefix() -> str:
    return f"retain/{uuid.uuid4().hex[:8]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker() -> tuple[str, int, object]:
    overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    process = start_broker(overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(overrides["network.mqtt_port"]), process


def _publish_retained(
    host: str,
    port: int,
    topic: str,
    payload: bytes,
    timeout_seconds: float,
    properties=None,
) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as publisher:
        assert_connack(
            publisher.connect(host, port, client_id=_unique_client_id("pub-retain"), clean_start=True),
            reason_code=0x00,
            session_present=False,
        )
        publisher.publish(topic, payload, qos=0, retain=True, properties=properties)


def run_4_1_1_publish_retain_stores_message(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/store"
        payload = b"retain-store"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-1-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "4.1.1 retained publish is stored and available"
    except Exception as error:
        return False, f"4.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_4_1_2_new_subscriber_gets_retained_immediately(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/new-subscriber"
        payload = b"retain-immediate"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-1-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            messages = subscriber.collect_messages(count=1, timeout=min(2.0, config.timeout_seconds))
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "4.1.2 new subscriber gets retained message directly after subscribe"
    except Exception as error:
        return False, f"4.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_4_1_3_retained_delivery_default_clears_retain_flag(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/retain-flag"
        payload = b"retain-flag-check"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-1-3"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "4.1.3 retained delivery clears RETAIN flag by default (RAP=0)"
    except Exception as error:
        return False, f"4.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_4_1_4_new_retained_replaces_old(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/replace"

        _publish_retained(host, port, topic, b"old-retain", config.timeout_seconds)
        _publish_retained(host, port, topic, b"new-retain", config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-1-4"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"new-retain", qos=0, retain=False)

        return True, "4.1.4 second retained publish replaces previous retained payload"
    except Exception as error:
        return False, f"4.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_4_2_1_empty_retained_publish_deletes_retained_message(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/delete"

        _publish_retained(host, port, topic, b"to-delete", config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            assert_connack(
                publisher.connect(host, port, client_id=_unique_client_id("pub-4-2-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            publisher.publish(topic, b"", qos=0, retain=True)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-2-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.2.1 retained message deleted by empty retained publish"
    except Exception as error:
        return False, f"4.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_4_2_2_no_retained_delivery_after_deletion(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/post-delete"

        _publish_retained(host, port, topic, b"delete-me", config.timeout_seconds)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            assert_connack(
                publisher.connect(host, port, client_id=_unique_client_id("pub-4-2-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            publisher.publish(topic, b"", qos=0, retain=True)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-2-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.2.2 new subscriber receives no retained message after deletion"
    except Exception as error:
        return False, f"4.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_4_3_1_retain_handling_zero_sends_on_every_subscribe(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/rh0"
        payload = b"rh0-payload"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        options = SubscribeOptions(qos=0, retainHandling=0, retainAsPublished=True)
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-3-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, options=options)
            first = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(first[0], topic=topic, payload=payload, qos=0, retain=True)

            subscriber.unsubscribe(topic)
            subscriber.subscribe(topic, options=options)
            second = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(second[0], topic=topic, payload=payload, qos=0, retain=True)

        return True, "4.3.1 retain handling 0 returns retained message on each subscribe"
    except Exception as error:
        return False, f"4.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_4_3_2_retain_handling_one_only_on_new_subscribe(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/rh1"
        payload = b"rh1-payload"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        options = SubscribeOptions(qos=0, retainHandling=1, retainAsPublished=True)
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-3-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, options=options)
            first = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(first[0], topic=topic, payload=payload, qos=0, retain=True)

            subscriber.subscribe(topic, options=options)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.3.2 retain handling 1 skips retained replay on re-subscribe"
    except Exception as error:
        return False, f"4.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_4_3_3_retain_handling_two_never_sends_retained(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/rh2"

        _publish_retained(host, port, topic, b"rh2-payload", config.timeout_seconds)

        options = SubscribeOptions(qos=0, retainHandling=2)
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-3-3"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, options=options)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.3.3 retain handling 2 suppresses retained delivery"
    except Exception as error:
        return False, f"4.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_4_4_1_rap_one_preserves_retain_flag(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/rap1"
        payload = b"rap-one"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        options = SubscribeOptions(qos=0, retainAsPublished=True)
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-4-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, options=options)
            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=True)

        return True, "4.4.1 retain as published=1 preserves retain flag"
    except Exception as error:
        return False, f"4.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_4_4_2_rap_zero_clears_retain_flag(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/rap0"
        payload = b"rap-zero"

        _publish_retained(host, port, topic, payload, config.timeout_seconds)

        options = SubscribeOptions(qos=0, retainAsPublished=False)
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-4-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, options=options)
            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "4.4.2 retain as published=0 clears retain flag on forwarded retained publish"
    except Exception as error:
        return False, f"4.4.2 failed: {error}"
    finally:
        stop_broker(process)


def run_4_5_1_retained_message_expires(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/expiry-check"
        payload = b"expires"

        publish_properties = Properties(PacketTypes.PUBLISH)
        setattr(publish_properties, "MessageExpiryInterval", 1)
        _publish_retained(host, port, topic, payload, config.timeout_seconds, properties=publish_properties)

        with MqttClient(timeout_seconds=config.timeout_seconds) as before_expiry_subscriber:
            assert_connack(
                before_expiry_subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-4-5-1-before"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            before_expiry_subscriber.subscribe(topic, qos=0)
            before_messages = before_expiry_subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(before_messages[0], topic=topic, payload=payload, qos=0, retain=False)

        time.sleep(3.2)

        with MqttClient(timeout_seconds=config.timeout_seconds) as after_expiry_subscriber:
            assert_connack(
                after_expiry_subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-4-5-1-after"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            after_expiry_subscriber.subscribe(topic, qos=0)
            assert_no_message(after_expiry_subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.5.1 retained message is removed after Message Expiry Interval"
    except Exception as error:
        return False, f"4.5.1 failed: {error}"
    finally:
        stop_broker(process)


def run_4_5_2_no_retained_delivery_after_expiry(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"{_run_prefix()}/expiry-no-delivery"

        publish_properties = Properties(PacketTypes.PUBLISH)
        setattr(publish_properties, "MessageExpiryInterval", 1)
        _publish_retained(host, port, topic, b"expires-soon", config.timeout_seconds, properties=publish_properties)

        time.sleep(3.2)

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-4-5-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe(topic, qos=0)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "4.5.2 subscriber after expiry receives no retained message"
    except Exception as error:
        return False, f"4.5.2 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "retain/store_and_deliver/stores_message",
        "description": "4.1.1 Publish with RETAIN=1 stores message",
        "run": run_4_1_1_publish_retain_stores_message,
    },
    {
        "name": "retain/store_and_deliver/new_subscriber_gets_retained",
        "description": "4.1.2 New subscriber receives retained message immediately",
        "run": run_4_1_2_new_subscriber_gets_retained_immediately,
    },
    {
        "name": "retain/store_and_deliver/retained_delivery_default_clears_flag",
        "description": "4.1.3 Retained delivery clears RETAIN flag by default",
        "run": run_4_1_3_retained_delivery_default_clears_retain_flag,
    },
    {
        "name": "retain/store_and_deliver/new_retained_replaces_old",
        "description": "4.1.4 New retained message replaces old retained message",
        "run": run_4_1_4_new_retained_replaces_old,
    },
    {
        "name": "retain/delete/empty_payload_deletes_retained",
        "description": "4.2.1 RETAIN=1 with empty payload deletes retained message",
        "run": run_4_2_1_empty_retained_publish_deletes_retained_message,
    },
    {
        "name": "retain/delete/new_subscriber_after_delete_gets_none",
        "description": "4.2.2 New subscriber after deletion gets no retained message",
        "run": run_4_2_2_no_retained_delivery_after_deletion,
    },
    {
        "name": "retain/retain_handling/zero_every_subscribe",
        "description": "4.3.1 Retain Handling=0 sends retained message on every subscribe",
        "run": run_4_3_1_retain_handling_zero_sends_on_every_subscribe,
    },
    {
        "name": "retain/retain_handling/one_only_new",
        "description": "4.3.2 Retain Handling=1 sends retained only on new subscribe",
        "run": run_4_3_2_retain_handling_one_only_on_new_subscribe,
    },
    {
        "name": "retain/retain_handling/two_never_send",
        "description": "4.3.3 Retain Handling=2 never sends retained on subscribe",
        "run": run_4_3_3_retain_handling_two_never_sends_retained,
    },
    {
        "name": "retain/retain_as_published/preserve_flag",
        "description": "4.4.1 Retain As Published=1 preserves RETAIN flag",
        "run": run_4_4_1_rap_one_preserves_retain_flag,
    },
    {
        "name": "retain/retain_as_published/clear_flag",
        "description": "4.4.2 Subscription with Retain As Published=0 clears RETAIN flag",
        "run": run_4_4_2_rap_zero_clears_retain_flag,
    },
    {
        "name": "retain/retained_expiry/message_removed_after_expiry",
        "description": "4.5.1 Retained message with expiry is removed after expiry",
        "run": run_4_5_1_retained_message_expires,
    },
    {
        "name": "retain/retained_expiry/new_subscriber_after_expiry_gets_none",
        "description": "4.5.2 New subscriber after expiry receives no retained message",
        "run": run_4_5_2_no_retained_delivery_after_expiry,
    },
]
