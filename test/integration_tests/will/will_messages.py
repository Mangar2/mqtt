"""Integration tests for Will Messages section 5.1 to 5.3."""

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
SubscribeOptions = _mqtt_client_module.SubscribeOptions


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/will/{prefix}/{uuid.uuid4().hex}"


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


def _new_will_properties(**values):
    properties = Properties(PacketTypes.WILLMESSAGE)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _force_abrupt_close(client: MqttClient) -> None:
    raw_client = getattr(client, "_client", None)
    if raw_client is None:
        raise RuntimeError("internal paho client is unavailable")

    client_socket = raw_client.socket()
    if client_socket is None:
        raise RuntimeError("client socket is unavailable")

    client_socket.close()
    raw_client.loop_stop()


def _stop_client_network_loop(client: MqttClient) -> None:
    raw_client = getattr(client, "_client", None)
    if raw_client is None:
        raise RuntimeError("internal paho client is unavailable")
    raw_client.loop_stop()


def _connect_subscriber(
    host: str,
    port: int,
    topic: str,
    timeout_seconds: float,
    qos: int = 1,
    options=None,
) -> MqttClient:
    subscriber = MqttClient(timeout_seconds=timeout_seconds)
    connack = subscriber.connect(
        host,
        port,
        client_id=_unique_client_id("will-sub"),
        clean_start=True,
    )
    assert_connack(connack, reason_code=0x00, session_present=False)

    if options is None:
        suback_codes = subscriber.subscribe(topic, qos=qos)
    else:
        suback_codes = subscriber.subscribe(topic, options=options)
    if not suback_codes:
        raise RuntimeError("missing SUBACK for will observer")

    reason_code = int(suback_codes[0])
    if reason_code not in (0x00, 0x01, 0x02):
        raise RuntimeError(f"unexpected SUBACK reason code 0x{reason_code:02X}")
    return subscriber


def _message_property(message, property_name: str):
    properties = getattr(message, "properties", None)
    if properties is None:
        return None
    return getattr(properties, property_name, None)


def run_5_1_1_will_on_abrupt_tcp_close(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("abrupt-close")
        payload = b"will-abrupt-close"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-abrupt"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.1.1 abrupt TCP close published will message"
    except Exception as error:
        return False, f"5.1.1 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_1_2_will_on_keepalive_timeout(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("keepalive-timeout")
        payload = b"will-keepalive-timeout"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-keepalive"),
            clean_start=True,
            keepalive=1,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        # Stop the paho loop to prevent automatic PINGREQ frames that would
        # keep the session alive indefinitely.
        _stop_client_network_loop(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(5.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.1.2 keep alive timeout published will message"
    except Exception as error:
        return False, f"5.1.2 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_1_3_disconnect_0x00_suppresses_will(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("disconnect-00")
        payload = b"will-should-not-publish"

        with _connect_subscriber(host, port, topic, config.timeout_seconds) as subscriber:
            with MqttClient(timeout_seconds=config.timeout_seconds) as will_client:
                will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
                connack = will_client.connect(
                    host,
                    port,
                    client_id=_unique_client_id("will-disconnect-00"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                will_client.disconnect(reason_code=0x00)

            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "5.1.3 DISCONNECT 0x00 suppressed will publication"
    except Exception as error:
        return False, f"5.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_5_1_4_disconnect_0x04_triggers_will(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("disconnect-04")
        payload = b"will-should-publish"

        with _connect_subscriber(host, port, topic, config.timeout_seconds) as subscriber:
            with MqttClient(timeout_seconds=config.timeout_seconds) as will_client:
                will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
                connack = will_client.connect(
                    host,
                    port,
                    client_id=_unique_client_id("will-disconnect-04"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                will_client.disconnect(reason_code=0x04)

            message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
            assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.1.4 DISCONNECT 0x04 triggered will publication"
    except Exception as error:
        return False, f"5.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_5_2_1_will_delay_zero_is_immediate(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("delay-zero")
        payload = b"will-delay-zero"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False, delay=0)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-delay-zero"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        start_time = time.monotonic()
        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        elapsed_seconds = time.monotonic() - start_time
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        if elapsed_seconds > 1.5:
            return False, f"will delivery was not immediate enough (elapsed={elapsed_seconds:.3f}s)"

        return True, "5.2.1 will delay 0 published immediately"
    except Exception as error:
        return False, f"5.2.1 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_2_2_will_delay_positive_publishes_after_delay(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("delay-positive")
        payload = b"will-delay-positive"
        will_delay_seconds = 2

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(
            topic=topic,
            payload=payload,
            qos=1,
            retain=False,
            delay=will_delay_seconds,
        )
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-delay-positive"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(3.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.2.2 will delay > 0 published will after delay"
    except Exception as error:
        return False, f"5.2.2 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_2_3_reconnect_before_delay_suppresses_will(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    reconnect_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("delay-reconnect")
        payload = b"will-delay-reconnect"
        client_id = _unique_client_id("will-reconnect")

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        connect_properties = _new_connect_properties(SessionExpiryInterval=10)
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False, delay=3)
        connack = will_client.connect(
            host,
            port,
            client_id=client_id,
            clean_start=True,
            keepalive=30,
            properties=connect_properties,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        time.sleep(0.8)

        reconnect_client = MqttClient(timeout_seconds=config.timeout_seconds)
        reconnect_connack = reconnect_client.connect(
            host,
            port,
            client_id=client_id,
            clean_start=False,
        )
        assert_connack(reconnect_connack, reason_code=0x00, session_present=True)

        assert_no_message(subscriber, timeout=min(0.5, config.timeout_seconds))
        reconnect_client.disconnect(reason_code=0x00)

        return True, "5.2.3 reconnect before Will Delay expiry suppressed will"
    except Exception as error:
        return False, f"5.2.3 failed: {error}"
    finally:
        if reconnect_client is not None:
            try:
                reconnect_client.disconnect()
            except Exception:
                pass
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_2_4_session_expiry_before_delay_publishes_will(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("delay-session-expiry")
        payload = b"will-session-expiry"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds)

        connect_properties = _new_connect_properties(SessionExpiryInterval=1)
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False, delay=5)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-session-expiry"),
            clean_start=True,
            keepalive=30,
            properties=connect_properties,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(3.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.2.4 session expiry before Will Delay published will at expiry"
    except Exception as error:
        return False, f"5.2.4 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_1_will_qos0_delivered_qos0(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("qos0")
        payload = b"will-qos0"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=1)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=0, retain=False)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-qos0"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=0, retain=False)

        return True, "5.3.1 will with QoS 0 delivered with QoS 0"
    except Exception as error:
        return False, f"5.3.1 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_2_will_qos1_delivered_qos1(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("qos1")
        payload = b"will-qos1"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=1)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=False)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-qos1"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "5.3.2 will with QoS 1 delivered with QoS 1"
    except Exception as error:
        return False, f"5.3.2 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_3_will_qos2_delivered_qos2(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("qos2")
        payload = b"will-qos2"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=2)

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=2, retain=False)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-qos2"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=2, retain=False)

        return True, "5.3.3 will with QoS 2 delivered with QoS 2"
    except Exception as error:
        return False, f"5.3.3 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_4_will_retain_stores_retained_message(config) -> tuple[bool, str]:
    process = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("retain")
        payload = b"will-retained"

        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(topic=topic, payload=payload, qos=1, retain=True)
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-retain"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        options = SubscribeOptions(qos=1, retainAsPublished=True)
        with _connect_subscriber(
            host,
            port,
            topic,
            config.timeout_seconds,
            qos=1,
            options=options,
        ) as retained_subscriber:
            message = retained_subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
            assert_message(message, topic=topic, payload=payload, qos=1, retain=True)

        return True, "5.3.4 retained will was stored as retained message"
    except Exception as error:
        return False, f"5.3.4 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_5_will_message_expiry_expires_retained_will(config) -> tuple[bool, str]:
    process = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("message-expiry")
        payload = b"will-expiring"

        expiry_seconds = 5
        will_properties = _new_will_properties(MessageExpiryInterval=expiry_seconds)
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(
            topic=topic,
            payload=payload,
            qos=1,
            retain=True,
            properties=will_properties,
        )
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-expiry"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        options = SubscribeOptions(qos=1, retainAsPublished=True)
        with _connect_subscriber(
            host,
            port,
            topic,
            config.timeout_seconds,
            qos=1,
            options=options,
        ) as first_subscriber:
            first_message = first_subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
            assert_message(first_message, topic=topic, payload=payload, qos=1, retain=True)

        time.sleep(float(expiry_seconds) + 1.5)

        with _connect_subscriber(
            host,
            port,
            topic,
            config.timeout_seconds,
            qos=1,
            options=options,
        ) as second_subscriber:
            assert_no_message(second_subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "5.3.5 retained will with expiry was removed after expiry"
    except Exception as error:
        return False, f"5.3.5 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_6_will_payload_format_and_content_type_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("format-content")
        payload = b"{\"kind\":\"will\"}"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=1)

        will_properties = _new_will_properties(
            PayloadFormatIndicator=1,
            ContentType="application/json",
        )
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(
            topic=topic,
            payload=payload,
            qos=1,
            retain=False,
            properties=will_properties,
        )
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-format"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        payload_format = _message_property(message, "PayloadFormatIndicator")
        if int(payload_format) != 1:
            return False, f"expected PayloadFormatIndicator=1, got {payload_format!r}"

        content_type = _message_property(message, "ContentType")
        if content_type != "application/json":
            return False, f"expected ContentType='application/json', got {content_type!r}"

        return True, "5.3.6 will Payload Format Indicator and Content Type forwarded"
    except Exception as error:
        return False, f"5.3.6 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_7_will_user_properties_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("user-properties")
        payload = b"will-user-properties"
        expected_user_properties = [("source", "will-test"), ("suite", "integration")]

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=1)

        will_properties = _new_will_properties(UserProperty=expected_user_properties)
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(
            topic=topic,
            payload=payload,
            qos=1,
            retain=False,
            properties=will_properties,
        )
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-user-props"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        actual_user_properties = _message_property(message, "UserProperty")
        if actual_user_properties is None:
            return False, "expected UserProperty on will publish, got none"

        for expected_pair in expected_user_properties:
            if expected_pair not in actual_user_properties:
                return False, (
                    f"expected UserProperty pair {expected_pair!r} in "
                    f"{actual_user_properties!r}"
                )

        return True, "5.3.7 will User Properties forwarded"
    except Exception as error:
        return False, f"5.3.7 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
            except Exception:
                pass
        if subscriber is not None:
            try:
                subscriber.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_5_3_8_will_response_topic_and_correlation_data_forwarded(config) -> tuple[bool, str]:
    process = None
    subscriber: MqttClient | None = None
    will_client: MqttClient | None = None
    try:
        host, port, process = _start_isolated_broker()
        topic = _unique_topic("response-correlation")
        payload = b"will-response-correlation"

        subscriber = _connect_subscriber(host, port, topic, config.timeout_seconds, qos=1)

        expected_response_topic = "integration/will/replies"
        expected_correlation_data = b"corr-will-538"
        will_properties = _new_will_properties(
            ResponseTopic=expected_response_topic,
            CorrelationData=expected_correlation_data,
        )
        will_client = MqttClient(timeout_seconds=config.timeout_seconds)
        will_client.set_will(
            topic=topic,
            payload=payload,
            qos=1,
            retain=False,
            properties=will_properties,
        )
        connack = will_client.connect(
            host,
            port,
            client_id=_unique_client_id("will-response-correlation"),
            clean_start=True,
            keepalive=30,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)

        _force_abrupt_close(will_client)

        message = subscriber.collect_messages(count=1, timeout=max(2.0, config.timeout_seconds))[0]
        assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        response_topic = _message_property(message, "ResponseTopic")
        if response_topic != expected_response_topic:
            return False, f"expected ResponseTopic={expected_response_topic!r}, got {response_topic!r}"

        correlation_data = _message_property(message, "CorrelationData")
        if correlation_data != expected_correlation_data:
            return False, (
                f"expected CorrelationData={expected_correlation_data!r}, "
                f"got {correlation_data!r}"
            )

        return True, "5.3.8 will Response Topic and Correlation Data forwarded"
    except Exception as error:
        return False, f"5.3.8 failed: {error}"
    finally:
        if will_client is not None:
            try:
                will_client.disconnect()
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
        "name": "will/trigger/abrupt_tcp_close_publishes_will",
        "description": "5.1.1 Abrupt TCP close publishes will message",
        "run": run_5_1_1_will_on_abrupt_tcp_close,
    },
    {
        "name": "will/trigger/keepalive_timeout_publishes_will",
        "description": "5.1.2 Keep alive timeout publishes will message",
        "run": run_5_1_2_will_on_keepalive_timeout,
    },
    {
        "name": "will/trigger/disconnect_00_suppresses_will",
        "description": "5.1.3 DISCONNECT 0x00 suppresses will publish",
        "run": run_5_1_3_disconnect_0x00_suppresses_will,
    },
    {
        "name": "will/trigger/disconnect_04_publishes_will",
        "description": "5.1.4 DISCONNECT 0x04 triggers will publish",
        "run": run_5_1_4_disconnect_0x04_triggers_will,
    },
    {
        "name": "will/will_delay/delay_zero_immediate",
        "description": "5.2.1 Will Delay 0 publishes immediately",
        "run": run_5_2_1_will_delay_zero_is_immediate,
    },
    {
        "name": "will/will_delay/delay_positive_after_interval",
        "description": "5.2.2 Will Delay > 0 publishes after delay",
        "run": run_5_2_2_will_delay_positive_publishes_after_delay,
    },
    {
        "name": "will/will_delay/reconnect_before_delay_suppresses",
        "description": "5.2.3 Reconnect before Will Delay expiry suppresses will",
        "run": run_5_2_3_reconnect_before_delay_suppresses_will,
    },
    {
        "name": "will/will_delay/session_expiry_before_delay_publishes",
        "description": "5.2.4 Session expiry before Will Delay publishes will",
        "run": run_5_2_4_session_expiry_before_delay_publishes_will,
    },
    {
        "name": "will/properties/will_qos0_delivered_qos0",
        "description": "5.3.1 Will QoS 0 delivered with QoS 0",
        "run": run_5_3_1_will_qos0_delivered_qos0,
    },
    {
        "name": "will/properties/will_qos1_delivered_qos1",
        "description": "5.3.2 Will QoS 1 delivered with QoS 1",
        "run": run_5_3_2_will_qos1_delivered_qos1,
    },
    {
        "name": "will/properties/will_qos2_delivered_qos2",
        "description": "5.3.3 Will QoS 2 delivered with QoS 2",
        "run": run_5_3_3_will_qos2_delivered_qos2,
    },
    {
        "name": "will/properties/will_retain_stored",
        "description": "5.3.4 Will with RETAIN=1 stored as retained message",
        "run": run_5_3_4_will_retain_stores_retained_message,
    },
    {
        "name": "will/properties/will_message_expiry_applied",
        "description": "5.3.5 Will with Message Expiry Interval expires",
        "run": run_5_3_5_will_message_expiry_expires_retained_will,
    },
    {
        "name": "will/properties/payload_format_and_content_type_forwarded",
        "description": "5.3.6 Will Payload Format Indicator and Content Type forwarded",
        "run": run_5_3_6_will_payload_format_and_content_type_forwarded,
    },
    {
        "name": "will/properties/user_properties_forwarded",
        "description": "5.3.7 Will User Properties forwarded",
        "run": run_5_3_7_will_user_properties_forwarded,
    },
    {
        "name": "will/properties/response_topic_and_correlation_data_forwarded",
        "description": "5.3.8 Will Response Topic and Correlation Data forwarded",
        "run": run_5_3_8_will_response_topic_and_correlation_data_forwarded,
    },
]
