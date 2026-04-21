"""Integration tests for load section 18.3 (subscription load)."""

from __future__ import annotations

from contextlib import ExitStack
import importlib.util
import os
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

assert_connack = _assertions_module.assert_connack
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient

_WRITE_QUEUE_BYTES_HIGH = 4 * 1024 * 1024
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"


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
    if os.environ.get(_BROKER_MANAGED_ENV, "").strip() != "0":
        effective_overrides["broker.write_queue_max_bytes"] = _WRITE_QUEUE_BYTES_HIGH
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _connect_client(host: str, port: int, timeout_seconds: float, prefix: str) -> MqttClient:
    client = MqttClient(timeout_seconds=timeout_seconds)
    connack = client.connect(host, port, client_id=_unique_client_id(prefix), clean_start=True)
    assert_connack(connack, reason_code=0x00, session_present=False)
    return client


def _collect_timeout(base_timeout_seconds: float, expected_messages: int) -> float:
    scaled_timeout = expected_messages / 200.0
    return max(base_timeout_seconds * 2.0, scaled_timeout, 8.0)


def _collect_topics_in_batches(client: MqttClient, *, total_count: int, timeout_seconds: float) -> set[str]:
    delivered_topics: set[str] = set()
    batch_size = 20
    remaining = total_count

    while remaining > 0:
        current_batch_size = min(batch_size, remaining)
        messages = client.collect_messages(
            count=current_batch_size,
            timeout=timeout_seconds,
        )
        for message in messages:
            delivered_topics.add(message.topic)
        remaining -= current_batch_size

    return delivered_topics


def run_18_3_1_one_client_subscribes_thousand_filters_all_active(config) -> tuple[bool, str]:
    process = None
    filter_count = 1000
    prefix = f"integration/load/subscription/18-3-1/{uuid.uuid4().hex}"

    try:
        host, port, process = _start_isolated_broker()
        with _connect_client(host, port, config.timeout_seconds, "sub-18-3-1") as subscriber:
            topics = [f"{prefix}/{index}" for index in range(filter_count)]

            for index, topic in enumerate(topics):
                suback_codes = subscriber.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, f"18.3.1 empty SUBACK for topic index {index}"
                assert_reason_code(suback_codes[0], 0x00)

            delivered_topics: set[str] = set()
            with _connect_client(host, port, config.timeout_seconds, "pub-18-3-1") as publisher:
                batch_topics: list[str] = []
                for index, topic in enumerate(topics):
                    payload = f"18.3.1-{index}".encode("utf-8")
                    assert_reason_code(publisher.publish(topic, payload, qos=0), 0x00)
                    batch_topics.append(topic)

                    if len(batch_topics) == 20:
                        delivered_topics.update(
                            _collect_topics_in_batches(
                                subscriber,
                                total_count=len(batch_topics),
                                timeout_seconds=_collect_timeout(config.timeout_seconds, len(batch_topics)),
                            )
                        )
                        batch_topics.clear()

                if batch_topics:
                    delivered_topics.update(
                        _collect_topics_in_batches(
                            subscriber,
                            total_count=len(batch_topics),
                            timeout_seconds=_collect_timeout(config.timeout_seconds, len(batch_topics)),
                        )
                    )

            expected_topics = set(topics)
            if delivered_topics != expected_topics:
                missing = len(expected_topics - delivered_topics)
                extra = len(delivered_topics - expected_topics)
                return False, f"18.3.1 topic coverage mismatch: missing={missing}, extra={extra}"

        return True, "18.3.1 1000 topic filters active and routable"
    except Exception as error:
        return False, f"18.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_18_3_2_hundred_clients_ten_filters_each_routing_correct(config) -> tuple[bool, str]:
    process = None
    client_count = 100
    filters_per_client = 10
    prefix = f"integration/load/subscription/18-3-2/{uuid.uuid4().hex}"

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            subscribers = [
                stack.enter_context(_connect_client(host, port, config.timeout_seconds, f"sub-18-3-2-{index}"))
                for index in range(client_count)
            ]
            publisher = stack.enter_context(_connect_client(host, port, config.timeout_seconds, "pub-18-3-2"))

            all_topics: list[str] = []
            topic_map: list[list[str]] = []
            for client_index, subscriber in enumerate(subscribers):
                current_topics = []
                for filter_index in range(filters_per_client):
                    topic = f"{prefix}/c{client_index}/f{filter_index}"
                    current_topics.append(topic)
                    all_topics.append(topic)
                    suback_codes = subscriber.subscribe(topic, qos=0)
                    if not suback_codes:
                        return False, f"18.3.2 empty SUBACK for client={client_index} filter={filter_index}"
                    assert_reason_code(suback_codes[0], 0x00)
                topic_map.append(current_topics)

            for topic_index, topic in enumerate(all_topics):
                payload = f"18.3.2-{topic_index}".encode("utf-8")
                assert_reason_code(publisher.publish(topic, payload, qos=0), 0x00)

            for client_index, subscriber in enumerate(subscribers):
                expected_topics = set(topic_map[client_index])
                messages = subscriber.collect_messages(
                    count=filters_per_client,
                    timeout=_collect_timeout(config.timeout_seconds, filters_per_client),
                )
                delivered_topics = {message.topic for message in messages}
                if delivered_topics != expected_topics:
                    return False, (
                        "18.3.2 routing mismatch "
                        f"for client={client_index}: expected={len(expected_topics)} got={len(delivered_topics)}"
                    )

        return True, "18.3.2 100 clients x 10 filters routed correctly"
    except Exception as error:
        return False, f"18.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_18_3_3_wildcard_subscription_thousand_matching_topics_correct_fanout(config) -> tuple[bool, str]:
    process = None
    topic_count = 1000
    root = f"integration/load/subscription/18-3-3/{uuid.uuid4().hex}"
    wildcard_filter = f"{root}/+/value"

    try:
        host, port, process = _start_isolated_broker()
        with _connect_client(host, port, config.timeout_seconds, "sub-18-3-3") as subscriber:
            suback_codes = subscriber.subscribe(wildcard_filter, qos=0)
            if not suback_codes:
                return False, "18.3.3 empty SUBACK for wildcard subscription"
            assert_reason_code(suback_codes[0], 0x00)

            with _connect_client(host, port, config.timeout_seconds, "pub-18-3-3") as publisher:
                expected_topics = []
                delivered_topics: set[str] = set()
                batch_count = 0
                for topic_index in range(topic_count):
                    topic = f"{root}/item-{topic_index}/value"
                    expected_topics.append(topic)
                    payload = f"18.3.3-{topic_index}".encode("utf-8")
                    assert_reason_code(publisher.publish(topic, payload, qos=0), 0x00)
                    batch_count += 1

                    if batch_count == 20:
                        delivered_topics.update(
                            _collect_topics_in_batches(
                                subscriber,
                                total_count=batch_count,
                                timeout_seconds=_collect_timeout(config.timeout_seconds, batch_count),
                            )
                        )
                        batch_count = 0

                if batch_count > 0:
                    delivered_topics.update(
                        _collect_topics_in_batches(
                            subscriber,
                            total_count=batch_count,
                            timeout_seconds=_collect_timeout(config.timeout_seconds, batch_count),
                        )
                    )

            if delivered_topics != set(expected_topics):
                missing = len(set(expected_topics) - delivered_topics)
                return False, f"18.3.3 wildcard fan-out mismatch, missing={missing}"

        return True, "18.3.3 wildcard subscription fanned out all 1000 matching topics"
    except Exception as error:
        return False, f"18.3.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "load/subscription_load_single_client_thousand_filters",
        "description": "18.3.1 1 client subscribes to 1000 filters -> all active",
        "run": run_18_3_1_one_client_subscribes_thousand_filters_all_active,
    },
    {
        "name": "load/subscription_load_hundred_clients_ten_filters_each",
        "description": "18.3.2 100 clients each subscribe to 10 filters -> routing correct for all",
        "run": run_18_3_2_hundred_clients_ten_filters_each_routing_correct,
    },
    {
        "name": "load/subscription_load_wildcard_thousand_matching_topics",
        "description": "18.3.3 Wildcard subscription with 1000 matching topics -> correct fan-out",
        "run": run_18_3_3_wildcard_subscription_thousand_matching_topics_correct_fanout,
    },
]
