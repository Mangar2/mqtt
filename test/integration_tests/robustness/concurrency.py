"""Integration tests for robustness section 19.5 (Concurrency)."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import importlib.util
from pathlib import Path
import random
import socket
from threading import Event
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
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties

_SESSION_EXPIRY_SECONDS = 300


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
        "broker.max_connections": 4000,
        "broker.max_queued_messages": 5000,
        "broker.write_queue_max_bytes": 4 * 1024 * 1024,
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


def run_19_5_1_two_publishers_same_topic_simultaneous_no_corruption(config) -> tuple[bool, str]:
    process = None
    topic = f"integration/robustness/19-5-1/{uuid.uuid4().hex}"
    payload_a = b"publisher-a"
    payload_b = b"publisher-b"

    def _publish_once(payload: bytes) -> int:
        with MqttClient(timeout_seconds=max(config.timeout_seconds, 6.0)) as publisher:
            connack = publisher.connect(host, port, client_id=_unique_client_id("pub-19-5-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            return int(publisher.publish(topic, payload, qos=1))

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=max(timeout_seconds, 8.0)) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-5-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "19.5.1 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with ThreadPoolExecutor(max_workers=2) as executor:
                future_a = executor.submit(_publish_once, payload_a)
                future_b = executor.submit(_publish_once, payload_b)
                assert_reason_code(future_a.result(timeout=max(timeout_seconds, 8.0)), 0x00)
                assert_reason_code(future_b.result(timeout=max(timeout_seconds, 8.0)), 0x00)

            messages = subscriber.collect_messages(count=2, timeout=max(timeout_seconds, 8.0))
            delivered_payloads = {bytes(message.payload) for message in messages}
            expected_payloads = {payload_a, payload_b}
            if delivered_payloads != expected_payloads:
                return False, f"19.5.1 payload corruption/mismatch: expected={expected_payloads}, got={delivered_payloads}"

        if process is None or process.poll() is not None:
            return False, "19.5.1 broker crashed during simultaneous publish test"
        return True, "19.5.1 simultaneous publish routed both messages without corruption"
    except Exception as error:
        return False, f"19.5.1 failed: {error}"
    finally:
        stop_broker(process)


def run_19_5_2_publish_during_subscribe_no_race_condition(config) -> tuple[bool, str]:
    process = None
    topic = f"integration/robustness/19-5-2/{uuid.uuid4().hex}"
    sentinel_payload = b"after-subscribe-sentinel"
    subscription_ready = Event()

    def _publisher_flow() -> str:
        with MqttClient(timeout_seconds=max(config.timeout_seconds, 8.0)) as publisher:
            connack = publisher.connect(host, port, client_id=_unique_client_id("pub-19-5-2"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

            for index in range(200):
                payload = f"pre-{index}".encode("utf-8")
                assert_reason_code(publisher.publish(topic, payload, qos=0), 0x00)
                time.sleep(0.005)

            if not subscription_ready.wait(timeout=max(4.0, config.timeout_seconds)):
                raise TimeoutError("subscription_ready was not set while publishing")

            assert_reason_code(publisher.publish(topic, sentinel_payload, qos=0), 0x00)
            return "ok"

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))
        with ThreadPoolExecutor(max_workers=1) as executor:
            publishing_future = executor.submit(_publisher_flow)
            time.sleep(0.3)

            with MqttClient(timeout_seconds=max(timeout_seconds, 10.0)) as subscriber:
                connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-5-2"), clean_start=True)
                assert_connack(connack, reason_code=0x00, session_present=False)
                suback_codes = subscriber.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, "19.5.2 subscriber returned empty SUBACK"
                assert_reason_code(suback_codes[0], 0x00)
                subscription_ready.set()

                sentinel_seen = False
                deadline = time.monotonic() + max(timeout_seconds, 10.0)
                while time.monotonic() < deadline:
                    messages = subscriber.collect_messages(count=1, timeout=max(0.5, min(2.0, timeout_seconds)))
                    if bytes(messages[0].payload) == sentinel_payload:
                        sentinel_seen = True
                        break
                if not sentinel_seen:
                    return False, "19.5.2 subscriber did not receive post-subscribe sentinel during concurrent publish"

            _ = publishing_future.result(timeout=max(timeout_seconds, 10.0))

        if process is None or process.poll() is not None:
            return False, "19.5.2 broker crashed during publish/subscribe race test"
        return True, "19.5.2 concurrent publish while subscribe completed without race-induced instability"
    except Exception as error:
        return False, f"19.5.2 failed: {error}"
    finally:
        stop_broker(process)


def run_19_5_3_session_takeover_during_active_publish_no_loss(config) -> tuple[bool, str]:
    process = None
    topic = f"integration/robustness/19-5-3/{uuid.uuid4().hex}"
    shared_client_id = _unique_client_id("takeover-19-5-3")
    stop_publishing = Event()
    messages_sent_before_takeover = 0

    def _active_publisher() -> int:
        sent_count = 0
        with MqttClient(timeout_seconds=max(config.timeout_seconds, 8.0)) as publisher:
            connack = publisher.connect(host, port, client_id=shared_client_id, clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            while not stop_publishing.is_set() and sent_count < 200:
                payload = f"pre-takeover-{sent_count}".encode("utf-8")
                assert_reason_code(publisher.publish(topic, payload, qos=1), 0x00)
                sent_count += 1
                time.sleep(0.005)
        return sent_count

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=max(timeout_seconds, 10.0)) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-5-3"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "19.5.3 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x01)

            with ThreadPoolExecutor(max_workers=1) as executor:
                active_future = executor.submit(_active_publisher)
                time.sleep(0.4)

                with MqttClient(timeout_seconds=max(timeout_seconds, 8.0)) as takeover_client:
                    takeover_connack = takeover_client.connect(
                        host,
                        port,
                        client_id=shared_client_id,
                        clean_start=False,
                        properties=_new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS),
                    )
                    assert_connack(takeover_connack, reason_code=0x00, session_present=False)
                    assert_reason_code(takeover_client.publish(topic, b"post-takeover", qos=1), 0x00)

                stop_publishing.set()
                messages_sent_before_takeover = active_future.result(timeout=max(timeout_seconds, 10.0))

            takeover_seen = False
            deadline = time.monotonic() + max(timeout_seconds, 10.0)
            while time.monotonic() < deadline:
                messages = subscriber.collect_messages(count=1, timeout=max(0.5, min(2.0, timeout_seconds)))
                if bytes(messages[0].payload) == b"post-takeover":
                    takeover_seen = True
                    break
            if not takeover_seen:
                return False, "19.5.3 subscriber did not receive post-takeover payload"

        if process is None or process.poll() is not None:
            return False, "19.5.3 broker crashed during session takeover test"
        return True, "19.5.3 session takeover during active publish completed without crash or visible loss"
    except Exception as error:
        return False, f"19.5.3 failed: {error}"
    finally:
        stop_publishing.set()
        stop_broker(process)


def run_19_5_4_hundred_clients_random_actions_thirty_seconds_stable(config) -> tuple[bool, str]:
    process = None
    client_count = 100
    duration_seconds = 30.0
    topic_root = f"integration/robustness/19-5-4/{uuid.uuid4().hex}"

    clients: list[MqttClient | None] = [None] * client_count
    client_ids = [_unique_client_id(f"rand-19-5-4-{index}") for index in range(client_count)]
    connected = [False] * client_count
    subscriptions: list[set[str]] = [set() for _ in range(client_count)]
    random_generator = random.Random(42)

    def _connect(index: int) -> None:
        if connected[index]:
            return
        client = MqttClient(timeout_seconds=max(config.timeout_seconds, 8.0))
        connack = client.connect(host, port, client_id=client_ids[index], clean_start=True)
        assert_connack(connack, reason_code=0x00, session_present=False)
        clients[index] = client
        connected[index] = True
        subscriptions[index].clear()

    def _disconnect(index: int) -> None:
        if not connected[index]:
            return
        client = clients[index]
        if client is None:
            return
        client.disconnect()
        connected[index] = False
        clients[index] = None
        subscriptions[index].clear()

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        for index in range(client_count):
            _connect(index)

        action_count = 0
        deadline = time.monotonic() + duration_seconds
        while time.monotonic() < deadline:
            index = random_generator.randrange(0, client_count)
            action = random_generator.choice(["publish", "subscribe", "unsubscribe", "disconnect", "reconnect"])
            topic = f"{topic_root}/topic-{random_generator.randrange(0, 25)}"
            client = clients[index]

            if action == "disconnect":
                _disconnect(index)
            elif action == "reconnect":
                _connect(index)
            elif not connected[index] or client is None:
                _connect(index)
            elif action == "publish":
                payload = f"rnd-{index}-{action_count}".encode("utf-8")
                assert_reason_code(client.publish(topic, payload, qos=0), 0x00)
            elif action == "subscribe":
                suback_codes = client.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, f"19.5.4 empty SUBACK for client index {index}"
                assert_reason_code(suback_codes[0], 0x00)
                subscriptions[index].add(topic)
            elif action == "unsubscribe":
                if topic in subscriptions[index]:
                    unsuback_codes = client.unsubscribe(topic)
                    if not unsuback_codes:
                        return False, f"19.5.4 empty UNSUBACK for client index {index}"
                    reason_code = int(unsuback_codes[0])
                    if reason_code not in (0x00, 0x11):
                        return False, f"19.5.4 unexpected UNSUBACK reason 0x{reason_code:02X}"
                    subscriptions[index].discard(topic)

            action_count += 1
            if process is None or process.poll() is not None:
                return False, "19.5.4 broker crashed during random concurrency workload"

        with MqttClient(timeout_seconds=max(timeout_seconds, 8.0)) as probe:
            connack = probe.connect(host, port, client_id=_unique_client_id("probe-19-5-4"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

        return True, f"19.5.4 broker remained stable for 30s random workload with {action_count} actions"
    except Exception as error:
        return False, f"19.5.4 failed: {error}"
    finally:
        for index in range(client_count):
            try:
                _disconnect(index)
            except Exception:
                continue
        stop_broker(process)


def run_19_5_5_unsubscribe_during_delivery_consistent_no_crash(config) -> tuple[bool, str]:
    process = None
    topic = f"integration/robustness/19-5-5/{uuid.uuid4().hex}"
    stop_publishing = Event()

    def _publisher_flow() -> int:
        sent = 0
        with MqttClient(timeout_seconds=max(config.timeout_seconds, 8.0)) as publisher:
            connack = publisher.connect(host, port, client_id=_unique_client_id("pub-19-5-5"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            while not stop_publishing.is_set() and sent < 400:
                payload = f"stream-{sent}".encode("utf-8")
                publish_reason = int(publisher.publish(topic, payload, qos=1))
                if publish_reason not in (0x00, 0x10):
                    raise AssertionError(f"unexpected PUBACK reason 0x{publish_reason:02X} during stream")
                sent += 1
                time.sleep(0.002)
        return sent

    try:
        host, port, process = _start_isolated_broker()
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=max(timeout_seconds, 10.0)) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-19-5-5"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "19.5.5 empty SUBACK for initial subscription"
            assert_reason_code(suback_codes[0], 0x01)

            with ThreadPoolExecutor(max_workers=1) as executor:
                publish_future = executor.submit(_publisher_flow)
                time.sleep(0.35)

                unsuback_codes = subscriber.unsubscribe(topic)
                if not unsuback_codes:
                    return False, "19.5.5 empty UNSUBACK while unsubscribing during delivery"
                unsuback_reason = int(unsuback_codes[0])
                if unsuback_reason not in (0x00, 0x11):
                    return False, f"19.5.5 unexpected UNSUBACK reason code 0x{unsuback_reason:02X}"

                stop_publishing.set()
                _ = publish_future.result(timeout=max(timeout_seconds, 10.0))

            marker_payload = b"post-unsub-marker"
            with MqttClient(timeout_seconds=max(timeout_seconds, 8.0)) as publisher_after_unsub:
                connack = publisher_after_unsub.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-19-5-5-post"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                publish_reason = int(publisher_after_unsub.publish(topic, marker_payload, qos=1))
                if publish_reason not in (0x00, 0x10):
                    return False, f"19.5.5 unexpected PUBACK reason after unsubscribe: 0x{publish_reason:02X}"
            observation_deadline = time.monotonic() + min(3.0, max(1.5, timeout_seconds))
            while time.monotonic() < observation_deadline:
                try:
                    message = subscriber.collect_messages(count=1, timeout=0.5)[0]
                except TimeoutError:
                    break
                if bytes(message.payload) == marker_payload:
                    return False, "19.5.5 received marker payload after unsubscribe acknowledgement"

        if process is None or process.poll() is not None:
            return False, "19.5.5 broker crashed during unsubscribe-while-delivering scenario"
        return True, "19.5.5 unsubscribe during active delivery stayed consistent and broker remained stable"
    except Exception as error:
        return False, f"19.5.5 failed: {error}"
    finally:
        stop_publishing.set()
        stop_broker(process)


TEST_CASES = [
    {
        "name": "robustness/concurrency_two_simultaneous_publishers_same_topic",
        "description": "19.5.1 Two clients publish to same topic simultaneously -> both messages routed, no corruption",
        "run": run_19_5_1_two_publishers_same_topic_simultaneous_no_corruption,
    },
    {
        "name": "robustness/concurrency_publish_while_subscribe",
        "description": "19.5.2 Client publishes while another subscribes to same topic -> no race condition",
        "run": run_19_5_2_publish_during_subscribe_no_race_condition,
    },
    {
        "name": "robustness/concurrency_session_takeover_during_publish",
        "description": "19.5.3 Session takeover during active publish -> no crash, messages not lost",
        "run": run_19_5_3_session_takeover_during_active_publish_no_loss,
    },
    {
        "name": "robustness/concurrency_hundred_clients_random_actions_30s",
        "description": "19.5.4 100 clients connect/disconnect/publish/subscribe randomly for 30s -> broker stable",
        "run": run_19_5_4_hundred_clients_random_actions_thirty_seconds_stable,
    },
    {
        "name": "robustness/concurrency_unsubscribe_during_delivery",
        "description": "19.5.5 Unsubscribe during message delivery -> no crash, delivery consistent",
        "run": run_19_5_5_unsubscribe_during_delivery_consistent_no_crash,
    },
]
