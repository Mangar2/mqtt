"""Integration tests for Will Messages section 5.1 and 5.2.1."""

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
    return "127.0.0.1", int(effective_overrides["network.mqtt_port"]), process


def _force_abrupt_close(client: MqttClient) -> None:
    raw_client = getattr(client, "_client", None)
    if raw_client is None:
        raise RuntimeError("internal paho client is unavailable")

    client_socket = raw_client.socket()
    if client_socket is None:
        raise RuntimeError("client socket is unavailable")

    client_socket.close()
    raw_client.loop_stop()


def _connect_subscriber(host: str, port: int, topic: str, timeout_seconds: float) -> MqttClient:
    subscriber = MqttClient(timeout_seconds=timeout_seconds)
    connack = subscriber.connect(
        host,
        port,
        client_id=_unique_client_id("will-sub"),
        clean_start=True,
    )
    assert_connack(connack, reason_code=0x00, session_present=False)

    suback_codes = subscriber.subscribe(topic, qos=1)
    if not suback_codes:
        raise RuntimeError("missing SUBACK for will observer")
    assert_reason_code(suback_codes[0], 0x01)
    return subscriber


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

        # Stop the paho network loop so it cannot send automatic PINGREQ
        # frames; otherwise keep alive timeout would never fire.
        raw_client = getattr(will_client, "_client", None)
        if raw_client is None:
            raise RuntimeError("internal paho client is unavailable")
        raw_client.loop_stop()

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
]
