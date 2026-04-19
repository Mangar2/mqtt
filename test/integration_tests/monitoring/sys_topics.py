"""Integration tests for Monitoring ($SYS Topics) section 15."""

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


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_monitoring_broker():
    mqtt_port = _find_free_port()
    overrides = {
        "network.mqtt_port": mqtt_port,
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
        "monitoring.sys_topic_interval": 1,
    }
    process = start_broker(overrides)
    return "127.0.0.1", mqtt_port, process


def _parse_int_payload(message_payload: bytes, topic: str) -> int:
    text_payload = message_payload.decode("utf-8", errors="strict").strip()
    try:
        return int(text_payload)
    except ValueError as value_error:
        raise AssertionError(
            f"non-integer payload on {topic}: {text_payload!r}"
        ) from value_error


def _wait_for_topic_value(client, topic: str, timeout_seconds: float) -> int:
    deadline = time.monotonic() + timeout_seconds
    last_non_matching_topic = None
    while time.monotonic() < deadline:
        remaining = max(0.05, deadline - time.monotonic())
        messages = client.collect_messages(count=1, timeout=min(remaining, 1.0))
        message = messages[0]
        if message.topic != topic:
            last_non_matching_topic = message.topic
            continue
        return _parse_int_payload(message.payload, topic)
    if last_non_matching_topic is None:
        raise TimeoutError(f"timed out waiting for topic {topic}")
    raise TimeoutError(
        f"timed out waiting for topic {topic}; last different topic was {last_non_matching_topic}"
    )


def _wait_for_incremented_value(
    client,
    topic: str,
    baseline_value: int,
    timeout_seconds: float,
) -> int:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        remaining = max(0.05, deadline - time.monotonic())
        current_value = _wait_for_topic_value(client, topic, min(remaining, 2.0))
        if current_value > baseline_value:
            return current_value
    raise TimeoutError(
        f"value on {topic} did not increase above baseline {baseline_value} within {timeout_seconds:.1f}s"
    )


def run_15_1_1_clients_connected_reflects_actual(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        topic = "$SYS/broker/clients/connected"
        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-1"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(topic, qos=0)
            baseline = _wait_for_topic_value(monitor, topic, timeout_seconds=5.0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as extra_client:
                assert_connack(
                    extra_client.connect(
                        host,
                        port,
                        client_id=_unique_client_id("extra-15-1-1"),
                        clean_start=True,
                    ),
                    reason_code=0x00,
                    session_present=False,
                )
                updated = _wait_for_incremented_value(monitor, topic, baseline, timeout_seconds=6.0)

        return True, f"15.1.1 clients/connected increased from {baseline} to {updated}"
    except Exception as error:
        return False, f"15.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_15_1_2_messages_received_increments(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        metric_topic = "$SYS/broker/messages/received"
        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-2"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(metric_topic, qos=0)
            baseline = _wait_for_topic_value(monitor, metric_topic, timeout_seconds=5.0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(
                        host,
                        port,
                        client_id=_unique_client_id("pub-15-1-2"),
                        clean_start=True,
                    ),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(f"t/monitoring/{uuid.uuid4().hex}/inbound", b"ping", qos=0)

            updated = _wait_for_incremented_value(monitor, metric_topic, baseline, timeout_seconds=6.0)

        return True, f"15.1.2 messages/received increased from {baseline} to {updated}"
    except Exception as error:
        return False, f"15.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_15_1_3_messages_sent_increments(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        metric_topic = "$SYS/broker/messages/sent"
        data_topic = f"t/monitoring/{uuid.uuid4().hex}/outbound"
        payload = b"outbound-message"

        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-3"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(metric_topic, qos=0)
            baseline = _wait_for_topic_value(monitor, metric_topic, timeout_seconds=5.0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
                assert_connack(
                    subscriber.connect(
                        host,
                        port,
                        client_id=_unique_client_id("sub-15-1-3"),
                        clean_start=True,
                    ),
                    reason_code=0x00,
                    session_present=False,
                )
                subscriber.subscribe(data_topic, qos=0)

                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                    assert_connack(
                        publisher.connect(
                            host,
                            port,
                            client_id=_unique_client_id("pub-15-1-3"),
                            clean_start=True,
                        ),
                        reason_code=0x00,
                        session_present=False,
                    )
                    publisher.publish(data_topic, payload, qos=0)

                delivered = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
                assert_message(delivered[0], topic=data_topic, payload=payload, qos=0, retain=False)

            updated = _wait_for_incremented_value(monitor, metric_topic, baseline, timeout_seconds=6.0)

        return True, f"15.1.3 messages/sent increased from {baseline} to {updated}"
    except Exception as error:
        return False, f"15.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_15_1_4_subscriptions_count_reflects_active(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        metric_topic = "$SYS/broker/subscriptions/count"
        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-4"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(metric_topic, qos=0)
            baseline = _wait_for_topic_value(monitor, metric_topic, timeout_seconds=5.0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
                assert_connack(
                    subscriber.connect(
                        host,
                        port,
                        client_id=_unique_client_id("sub-15-1-4"),
                        clean_start=True,
                    ),
                    reason_code=0x00,
                    session_present=False,
                )
                topic_a = f"t/monitoring/{uuid.uuid4().hex}/sub/a"
                topic_b = f"t/monitoring/{uuid.uuid4().hex}/sub/b"
                subscriber.subscribe(topic_a, qos=0)
                subscriber.subscribe(topic_b, qos=0)
                updated = _wait_for_incremented_value(monitor, metric_topic, baseline, timeout_seconds=6.0)

        return True, f"15.1.4 subscriptions/count increased from {baseline} to {updated}"
    except Exception as error:
        return False, f"15.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_15_1_5_retained_count_reflects_stored(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        metric_topic = "$SYS/broker/retained messages/count"
        retained_topic = f"t/monitoring/{uuid.uuid4().hex}/retained"

        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-5"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(metric_topic, qos=0)
            baseline = _wait_for_topic_value(monitor, metric_topic, timeout_seconds=5.0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(
                        host,
                        port,
                        client_id=_unique_client_id("pub-15-1-5"),
                        clean_start=True,
                    ),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(retained_topic, b"retained-value", qos=0, retain=True)

            updated = _wait_for_incremented_value(monitor, metric_topic, baseline, timeout_seconds=6.0)

        return True, f"15.1.5 retained count increased from {baseline} to {updated}"
    except Exception as error:
        return False, f"15.1.5 failed: {error}"
    finally:
        stop_broker(process)


def run_15_1_6_uptime_increases_over_time(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        topic = "$SYS/broker/uptime"

        with MqttClient(timeout_seconds=config.timeout_seconds) as monitor:
            assert_connack(
                monitor.connect(
                    host,
                    port,
                    client_id=_unique_client_id("mon-15-1-6"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            monitor.subscribe(topic, qos=0)
            first_value = _wait_for_topic_value(monitor, topic, timeout_seconds=5.0)
            second_value = _wait_for_incremented_value(monitor, topic, first_value, timeout_seconds=6.0)

        return True, f"15.1.6 uptime increased from {first_value} to {second_value}"
    except Exception as error:
        return False, f"15.1.6 failed: {error}"
    finally:
        stop_broker(process)


def run_15_2_1_sys_hash_receives_periodic_updates(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-15-2-1"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe("$SYS/#", qos=0)
            messages = subscriber.collect_messages(count=2, timeout=8.0)
            for message in messages:
                if not message.topic.startswith("$SYS/"):
                    return False, f"15.2.1 non-$SYS topic received on $SYS/#: {message.topic}"

        return True, f"15.2.1 $SYS/# subscription received {len(messages)} periodic updates"
    except Exception as error:
        return False, f"15.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_15_2_2_hash_does_not_receive_sys_topics(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_monitoring_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-15-2-2"),
                    clean_start=True,
                ),
                reason_code=0x00,
                session_present=False,
            )
            subscriber.subscribe("#", qos=0)
            assert_no_message(subscriber, timeout=min(2.5, config.timeout_seconds))

        return True, "15.2.2 # subscription received no $SYS topics"
    except Exception as error:
        return False, f"15.2.2 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "monitoring/clients_connected_reflects_actual_connected_clients",
        "description": "15.1.1 $SYS/broker/clients/connected reflects actual connected clients",
        "run": run_15_1_1_clients_connected_reflects_actual,
    },
    {
        "name": "monitoring/messages_received_increments_on_inbound_publish",
        "description": "15.1.2 $SYS/broker/messages/received increments on inbound PUBLISH",
        "run": run_15_1_2_messages_received_increments,
    },
    {
        "name": "monitoring/messages_sent_increments_on_outbound_publish",
        "description": "15.1.3 $SYS/broker/messages/sent increments on outbound PUBLISH",
        "run": run_15_1_3_messages_sent_increments,
    },
    {
        "name": "monitoring/subscriptions_count_reflects_active_subscriptions",
        "description": "15.1.4 $SYS/broker/subscriptions/count reflects active subscriptions",
        "run": run_15_1_4_subscriptions_count_reflects_active,
    },
    {
        "name": "monitoring/retained_messages_count_reflects_stored_retained_messages",
        "description": "15.1.5 $SYS/broker/retained messages/count reflects stored retained messages",
        "run": run_15_1_5_retained_count_reflects_stored,
    },
    {
        "name": "monitoring/uptime_increases_over_time",
        "description": "15.1.6 $SYS/broker/uptime increases over time",
        "run": run_15_1_6_uptime_increases_over_time,
    },
    {
        "name": "monitoring/sys_hash_receives_periodic_updates",
        "description": "15.2.1 Subscribe $SYS/# receives periodic updates",
        "run": run_15_2_1_sys_hash_receives_periodic_updates,
    },
    {
        "name": "monitoring/hash_does_not_receive_sys_topics",
        "description": "15.2.2 Subscribe # does NOT receive $SYS topics",
        "run": run_15_2_2_hash_does_not_receive_sys_topics,
    },
]
