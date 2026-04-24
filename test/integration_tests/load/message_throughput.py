"""Integration tests for load section 18.2 (message throughput)."""

from __future__ import annotations

from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import socket
import time
import uuid


_PAYLOAD_256KB_BYTES = 256 * 1024
_WRITE_QUEUE_BYTES_ALLOW_256KB = 300 * 1024
_WRITE_QUEUE_BYTES_REJECT_256KB = 200 * 1024
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"
_REMOTE_RTT_BUDGET_SECONDS = 0.05


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
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/load/throughput/{prefix}/{uuid.uuid4().hex}"


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


def _connect_client(host: str, port: int, timeout_seconds: float, client_prefix: str) -> MqttClient:
    client = MqttClient(timeout_seconds=timeout_seconds)
    connack = client.connect(
        host,
        port,
        client_id=_unique_client_id(client_prefix),
        clean_start=True,
    )
    assert_connack(connack, reason_code=0x00, session_present=False)
    return client


def _collect_timeout(base_timeout_seconds: float, expected_messages: int) -> float:
    scaled_timeout = expected_messages / 250.0
    return max(base_timeout_seconds * 2.0, scaled_timeout, 6.0)


def _collect_timeout_remote_aware(base_timeout_seconds: float, expected_messages: int) -> float:
    timeout_seconds = _collect_timeout(base_timeout_seconds, expected_messages)
    if not _is_remote_unmanaged_mode():
        return timeout_seconds

    # Remote mode tolerance: absorb network RTT variance for serial waits.
    return timeout_seconds + max(4.0, expected_messages * _REMOTE_RTT_BUDGET_SECONDS * 0.2)


def _qos0_publish_spacing_seconds(total_messages: int) -> float:
    if total_messages >= 1000:
        return 0.001
    if total_messages >= 100:
        return 0.0005
    return 0.0


def _extract_received_count_from_timeout(timeout_text: str) -> int | None:
    marker = "got "
    marker_index = timeout_text.rfind(marker)
    if marker_index < 0:
        return None

    count_start = marker_index + len(marker)
    count_chars: list[str] = []
    for char in timeout_text[count_start:]:
        if not char.isdigit():
            break
        count_chars.append(char)

    if not count_chars:
        return None

    try:
        return int("".join(count_chars))
    except ValueError:
        return None


def _is_remote_unmanaged_mode() -> bool:
    return os.environ.get(_BROKER_MANAGED_ENV, "").strip() == "0"


def run_18_2_1_one_pub_one_sub_qos0_thousand_under_five_seconds(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-1")
    message_count = 1000

    try:
        host, port, process = _start_isolated_broker()
        with _connect_client(host, port, config.timeout_seconds, "sub-18-2-1") as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "18.2.1 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x00)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-1") as publisher:
                start_time = time.monotonic()
                spacing_seconds = _qos0_publish_spacing_seconds(message_count)
                for message_index in range(message_count):
                    payload = f"18.2.1-{message_index}".encode("utf-8")
                    publish_reason = publisher.publish(
                        topic,
                        payload,
                        qos=0,
                        wait_for_qos0_publish=False,
                    )
                    assert_reason_code(publish_reason, 0x00)
                    if spacing_seconds > 0.0:
                        time.sleep(spacing_seconds)

                received_messages = subscriber.collect_messages(
                    count=message_count,
                    timeout=_collect_timeout_remote_aware(config.timeout_seconds, message_count),
                )
                elapsed_seconds = time.monotonic() - start_time

            if len(received_messages) != message_count:
                return False, f"18.2.1 expected {message_count} messages, got {len(received_messages)}"

            throughput_target_seconds = 5.0
            if _is_remote_unmanaged_mode():
                # Remote mode: allow extra network latency while preserving strict local target.
                throughput_target_seconds = 15.0

            if elapsed_seconds >= throughput_target_seconds:
                return (
                    False,
                    f"18.2.1 exceeded {throughput_target_seconds:.1f} second target: {elapsed_seconds:.3f}s",
                )

        return (
            True,
            f"18.2.1 delivered {message_count} QoS0 messages in {elapsed_seconds:.3f}s"
            f" (target {throughput_target_seconds:.1f}s)",
        )
    except Exception as error:
        return False, f"18.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_2_one_pub_one_sub_qos1_thousand_acked_and_delivered(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-2")
    message_count = 1000

    try:
        host, port, process = _start_isolated_broker()
        with _connect_client(host, port, config.timeout_seconds, "sub-18-2-2") as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "18.2.2 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-2") as publisher:
                for message_index in range(message_count):
                    payload = f"18.2.2-{message_index}".encode("utf-8")
                    publish_reason = publisher.publish(topic, payload, qos=1)
                    assert_reason_code(publish_reason, 0x00)

                received_messages = subscriber.collect_messages(
                    count=message_count,
                    timeout=_collect_timeout(config.timeout_seconds, message_count),
                )

            if len(received_messages) != message_count:
                return False, f"18.2.2 expected {message_count} messages, got {len(received_messages)}"

        return True, f"18.2.2 all {message_count} QoS1 messages were ACKed and delivered"
    except Exception as error:
        return False, f"18.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_3_ten_publishers_ten_subscribers_hundred_each(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-3")
    publisher_count = 10
    subscriber_count = 10
    messages_per_publisher = 100
    expected_publications = publisher_count * messages_per_publisher
    expected_deliveries = expected_publications * subscriber_count

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            subscribers = [
                stack.enter_context(_connect_client(host, port, config.timeout_seconds, f"sub-18-2-3-{subscriber_index}"))
                for subscriber_index in range(subscriber_count)
            ]
            publishers = [
                stack.enter_context(_connect_client(host, port, config.timeout_seconds, f"pub-18-2-3-{publisher_index}"))
                for publisher_index in range(publisher_count)
            ]

            for subscriber_index, subscriber in enumerate(subscribers):
                suback_codes = subscriber.subscribe(topic, qos=1)
                if not suback_codes:
                    return False, f"18.2.3 subscriber {subscriber_index} returned empty SUBACK"
                assert_reason_code(suback_codes[0], 0x01)

            for publisher_index, publisher in enumerate(publishers):
                for message_index in range(messages_per_publisher):
                    payload = f"18.2.3-p{publisher_index}-m{message_index}".encode("utf-8")
                    publish_reason = publisher.publish(topic, payload, qos=1)
                    assert_reason_code(publish_reason, 0x00)

            delivery_total = 0
            for subscriber in subscribers:
                received_messages = subscriber.collect_messages(
                    count=expected_publications,
                    timeout=_collect_timeout(config.timeout_seconds, expected_publications),
                )
                delivery_total += len(received_messages)

            if delivery_total != expected_deliveries:
                return False, f"18.2.3 expected {expected_deliveries} deliveries, got {delivery_total}"

        return True, f"18.2.3 completed {expected_deliveries} total fan-out deliveries"
    except Exception as error:
        return False, f"18.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_4_one_publisher_hundred_subscribers_hundred_messages(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-4")
    subscriber_count = 100
    message_count = 100
    expected_deliveries = subscriber_count * message_count

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            subscribers = [
                stack.enter_context(_connect_client(host, port, config.timeout_seconds, f"sub-18-2-4-{subscriber_index}"))
                for subscriber_index in range(subscriber_count)
            ]

            for subscriber_index, subscriber in enumerate(subscribers):
                suback_codes = subscriber.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, f"18.2.4 subscriber {subscriber_index} returned empty SUBACK"
                assert_reason_code(suback_codes[0], 0x00)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-4") as publisher:
                spacing_seconds = _qos0_publish_spacing_seconds(message_count)
                for message_index in range(message_count):
                    payload = f"18.2.4-{message_index}".encode("utf-8")
                    publish_reason = publisher.publish(topic, payload, qos=0)
                    assert_reason_code(publish_reason, 0x00)
                    if spacing_seconds > 0.0:
                        time.sleep(spacing_seconds)

            delivery_total = 0
            delivery_timeout = _collect_timeout_remote_aware(config.timeout_seconds, message_count)
            delivery_deadline = time.monotonic() + delivery_timeout
            subscriber_received_counts = [0 for _ in range(subscriber_count)]

            # Fair collection across all subscribers in one shared window.
            while time.monotonic() < delivery_deadline:
                made_progress = False
                for subscriber_index, subscriber in enumerate(subscribers):
                    drained_messages = subscriber.drain_available_messages()
                    if not drained_messages:
                        continue
                    drained_count = len(drained_messages)
                    subscriber_received_counts[subscriber_index] += drained_count
                    delivery_total += drained_count
                    made_progress = True

                if delivery_total >= expected_deliveries:
                    break

                if not made_progress:
                    time.sleep(0.01)

            # Final non-blocking drain after the timeout window.
            for subscriber_index, subscriber in enumerate(subscribers):
                drained_messages = subscriber.drain_available_messages()
                if not drained_messages:
                    continue
                drained_count = len(drained_messages)
                subscriber_received_counts[subscriber_index] += drained_count
                delivery_total += drained_count

            underfilled_subscribers = [
                (subscriber_index, received_count)
                for subscriber_index, received_count in enumerate(subscriber_received_counts)
                if received_count != message_count
            ]

            if delivery_total != expected_deliveries:
                min_count = min(subscriber_received_counts) if subscriber_received_counts else 0
                max_count = max(subscriber_received_counts) if subscriber_received_counts else 0
                delivery_percent = (delivery_total / expected_deliveries) * 100.0 if expected_deliveries > 0 else 0.0
                lowest_subscribers = sorted(underfilled_subscribers, key=lambda item: item[1])[:10]
                lowest_text = ", ".join(
                    f"sub[{subscriber_index}]={received_count}"
                    for subscriber_index, received_count in lowest_subscribers
                )
                return (
                    False,
                    "18.2.4 received "
                    f"{delivery_total}/{expected_deliveries} deliveries "
                    f"({delivery_percent:.1f}%); "
                    f"underfilled={len(underfilled_subscribers)}/{subscriber_count}; "
                    f"min_per_subscriber={min_count}; max_per_subscriber={max_count}; "
                    f"lowest={lowest_text or 'none'}",
                )

        return True, f"18.2.4 completed {expected_deliveries} total deliveries"
    except Exception as error:
        return False, f"18.2.4 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_5_large_payload_256kb_qos0_delivered(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-5")
    payload = bytes((index % 251 for index in range(_PAYLOAD_256KB_BYTES)))

    try:
        overrides = None
        if not _is_remote_unmanaged_mode():
            overrides = {"broker.write_queue_max_bytes": _WRITE_QUEUE_BYTES_ALLOW_256KB}
        host, port, process = _start_isolated_broker(overrides)
        with _connect_client(host, port, config.timeout_seconds, "sub-18-2-5") as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "18.2.5 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x00)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-5") as publisher:
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            received_messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=max(config.timeout_seconds * 2.0, 8.0))
            assert_message(received_messages, topic=topic, payload=payload, qos=0, retain=False)

        return True, "18.2.5 delivered 256KB payload at QoS0"
    except Exception as error:
        return False, f"18.2.5 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_6_large_payload_256kb_qos1_acked_and_delivered(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-6")
    payload = bytes((index % 253 for index in range(_PAYLOAD_256KB_BYTES)))

    try:
        overrides = None
        if not _is_remote_unmanaged_mode():
            overrides = {"broker.write_queue_max_bytes": _WRITE_QUEUE_BYTES_ALLOW_256KB}
        host, port, process = _start_isolated_broker(overrides)
        with _connect_client(host, port, config.timeout_seconds, "sub-18-2-6") as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "18.2.6 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-6") as publisher:
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            received_messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=max(config.timeout_seconds * 2.0, 8.0))
            assert_message(received_messages, topic=topic, payload=payload, qos=1, retain=False)

        return True, "18.2.6 delivered and ACKed 256KB payload at QoS1"
    except Exception as error:
        return False, f"18.2.6 failed: {error}"
    finally:
        stop_broker(process)


def run_18_2_7_large_payload_256kb_qos1_quota_exceeded(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("18-2-7")
    payload = bytes((index % 249 for index in range(_PAYLOAD_256KB_BYTES)))

    try:
        host, port, process = _start_isolated_broker(
            {"broker.write_queue_max_bytes": _WRITE_QUEUE_BYTES_REJECT_256KB}
        )

        with _connect_client(host, port, config.timeout_seconds, "sub-18-2-7") as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "18.2.7 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-2-7") as publisher:
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x97)

            try:
                subscriber.collect_messages(count=1, timeout=max(config.timeout_seconds, 3.0))
            except TimeoutError:
                return (
                    True,
                    "18.2.7 returned QoS1 QuotaExceeded (0x97) for 256KB payload "
                    f"with write_queue_max_bytes={_WRITE_QUEUE_BYTES_REJECT_256KB}",
                )

            return False, "18.2.7 subscriber unexpectedly received oversized QoS1 payload"
    except Exception as error:
        return False, f"18.2.7 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "load/message_throughput_single_pub_sub_qos0_1000_under_5s",
        "description": "18.2.1 1 publisher, 1 subscriber, 1000 QoS0 messages -> all delivered in under 5s",
        "run": run_18_2_1_one_pub_one_sub_qos0_thousand_under_five_seconds,
    },
    {
        "name": "load/message_throughput_single_pub_sub_qos1_1000_acked_and_delivered",
        "description": "18.2.2 1 publisher, 1 subscriber, 1000 QoS1 messages -> all ACKed and delivered",
        "run": run_18_2_2_one_pub_one_sub_qos1_thousand_acked_and_delivered,
    },
    {
        "name": "load/message_throughput_ten_publishers_ten_subscribers_fanout",
        "description": "18.2.3 10 publishers, 10 subscribers, 100 messages each -> 10000 deliveries total",
        "run": run_18_2_3_ten_publishers_ten_subscribers_hundred_each,
    },
    {
        "name": "load/message_throughput_one_publisher_hundred_subscribers",
        "description": "18.2.4 1 publisher, 100 subscribers, 100 messages -> 10000 deliveries total",
        "run": run_18_2_4_one_publisher_hundred_subscribers_hundred_messages,
    },
    {
        "name": "load/message_throughput_large_payload_256kb_qos0",
        "description": "18.2.5 Large payload 256KB QoS0 -> delivered correctly",
        "run": run_18_2_5_large_payload_256kb_qos0_delivered,
    },
    {
        "name": "load/message_throughput_large_payload_256kb_qos1",
        "description": "18.2.6 Large payload 256KB QoS1 -> delivered and ACKed",
        "run": run_18_2_6_large_payload_256kb_qos1_acked_and_delivered,
    },
    {
        "name": "load/message_throughput_maximum_packet_size_boundary",
        "description": "18.2.7 256KB QoS1 with lower write queue cap -> PUBACK QuotaExceeded (0x97)",
        "run": run_18_2_7_large_payload_256kb_qos1_quota_exceeded,
    },
]
