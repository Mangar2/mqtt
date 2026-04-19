"""Integration tests for multi-client scenarios section 17.1 to 17.4."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import importlib.util
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
_raw_tcp_module = _load_helper("raw_tcp")

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
flood_connections = _raw_tcp_module.flood_connections
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/multi-client/{prefix}/{uuid.uuid4().hex}"


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


def run_17_1_1_one_publisher_ten_subscribers_receive(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("17-1-1")
    payload = b"fan-out-to-ten"

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            subscribers = [
                stack.enter_context(MqttClient(timeout_seconds=config.timeout_seconds))
                for _ in range(10)
            ]

            for index, subscriber in enumerate(subscribers):
                connack = subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id(f"sub-17-1-1-{index}"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                suback_codes = subscriber.subscribe(topic, qos=0)
                if not suback_codes:
                    return False, f"17.1.1 subscriber {index} returned empty SUBACK"
                assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-17-1-1"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            for index, subscriber in enumerate(subscribers):
                messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
                assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "17.1.1 all 10 subscribers received the published message"
    except Exception as error:
        return False, f"17.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_17_1_2_mixed_qos_each_subscriber_gets_subscribed_qos(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("17-1-2")
    payload = b"fan-out-mixed-qos"
    subscriber_qos_levels = [0, 1, 2, 0, 1, 2]

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            subscribers = [
                stack.enter_context(MqttClient(timeout_seconds=config.timeout_seconds))
                for _ in subscriber_qos_levels
            ]

            for index, (subscriber, subscribed_qos) in enumerate(zip(subscribers, subscriber_qos_levels)):
                connack = subscriber.connect(
                    host,
                    port,
                    client_id=_unique_client_id(f"sub-17-1-2-{index}"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                suback_codes = subscriber.subscribe(topic, qos=subscribed_qos)
                if not suback_codes:
                    return False, f"17.1.2 subscriber {index} returned empty SUBACK"
                assert_reason_code(suback_codes[0], subscribed_qos)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-17-1-2"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            delivered_qos: list[int] = []
            for subscriber, subscribed_qos in zip(subscribers, subscriber_qos_levels):
                messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
                message = messages[0]
                assert_message(message, topic=topic, payload=payload, qos=subscribed_qos, retain=False)
                delivered_qos.append(int(message.qos))

        return True, f"17.1.2 mixed QoS fan-out delivered with QoS levels {delivered_qos}"
    except Exception as error:
        return False, f"17.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_17_2_1_ten_publishers_one_subscriber_receives_all(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("17-2-1")
    expected_payloads = [f"fan-in-msg-{index}".encode("utf-8") for index in range(10)]

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-17-2-1"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "17.2.1 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x00)

            for index, payload in enumerate(expected_payloads):
                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                    publisher_connack = publisher.connect(
                        host,
                        port,
                        client_id=_unique_client_id(f"pub-17-2-1-{index}"),
                        clean_start=True,
                    )
                    assert_connack(publisher_connack, reason_code=0x00, session_present=False)
                    publish_reason = publisher.publish(topic, payload, qos=0)
                    assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=10, timeout=config.timeout_seconds)
            received_payloads = [bytes(message.payload) for message in messages]
            if set(received_payloads) != set(expected_payloads):
                return False, (
                    "17.2.1 payload mismatch: "
                    f"expected={sorted(payload.decode('utf-8') for payload in expected_payloads)}, "
                    f"got={sorted(payload.decode('utf-8') for payload in received_payloads)}"
                )

        return True, "17.2.1 subscriber received all 10 messages from 10 publishers"
    except Exception as error:
        return False, f"17.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_17_3_1_cross_traffic_routes_without_contamination(config) -> tuple[bool, str]:
    process = None
    topic_a = _unique_topic("17-3-1-a")
    topic_b = _unique_topic("17-3-1-b")
    payload_a = b"cross-traffic-a"
    payload_b = b"cross-traffic-b"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber_a:
            with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber_b:
                connack_a = subscriber_a.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-17-3-1-a"),
                    clean_start=True,
                )
                assert_connack(connack_a, reason_code=0x00, session_present=False)
                connack_b = subscriber_b.connect(
                    host,
                    port,
                    client_id=_unique_client_id("sub-17-3-1-b"),
                    clean_start=True,
                )
                assert_connack(connack_b, reason_code=0x00, session_present=False)

                suback_a = subscriber_a.subscribe(topic_a, qos=0)
                suback_b = subscriber_b.subscribe(topic_b, qos=0)
                if not suback_a or not suback_b:
                    return False, "17.3.1 one or more subscribers returned empty SUBACK"
                assert_reason_code(suback_a[0], 0x00)
                assert_reason_code(suback_b[0], 0x00)

                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher_a:
                    pub_connack_a = publisher_a.connect(
                        host,
                        port,
                        client_id=_unique_client_id("pub-17-3-1-a"),
                        clean_start=True,
                    )
                    assert_connack(pub_connack_a, reason_code=0x00, session_present=False)
                    assert_reason_code(publisher_a.publish(topic_a, payload_a, qos=0), 0x00)

                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher_b:
                    pub_connack_b = publisher_b.connect(
                        host,
                        port,
                        client_id=_unique_client_id("pub-17-3-1-b"),
                        clean_start=True,
                    )
                    assert_connack(pub_connack_b, reason_code=0x00, session_present=False)
                    assert_reason_code(publisher_b.publish(topic_b, payload_b, qos=0), 0x00)

                messages_a = subscriber_a.collect_messages(count=1, timeout=config.timeout_seconds)
                messages_b = subscriber_b.collect_messages(count=1, timeout=config.timeout_seconds)
                assert_message(messages_a[0], topic=topic_a, payload=payload_a, qos=0, retain=False)
                assert_message(messages_b[0], topic=topic_b, payload=payload_b, qos=0, retain=False)
                assert_no_message(subscriber_a, timeout=min(1.0, config.timeout_seconds))
                assert_no_message(subscriber_b, timeout=min(1.0, config.timeout_seconds))

        return True, "17.3.1 cross-traffic routed correctly with no topic contamination"
    except Exception as error:
        return False, f"17.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_17_3_2_client_publishes_and_receives_own_message(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("17-3-2")
    payload = b"self-publish"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("client-17-3-2"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "17.3.2 subscriber returned empty SUBACK"
            assert_reason_code(suback_codes[0], 0x00)
            assert_reason_code(client.publish(topic, payload, qos=0), 0x00)
            messages = client.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "17.3.2 client received own message after publishing to subscribed topic"
    except Exception as error:
        return False, f"17.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_17_4_1_rapid_connect_disconnect_hundred_times(config) -> tuple[bool, str]:
    process = None

    try:
        host, port, process = _start_isolated_broker()
        for index in range(100):
            with MqttClient(timeout_seconds=config.timeout_seconds) as client:
                connack = client.connect(
                    host,
                    port,
                    client_id=_unique_client_id(f"rapid-17-4-1-{index}"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)

        with MqttClient(timeout_seconds=config.timeout_seconds) as probe_client:
            probe_connack = probe_client.connect(
                host,
                port,
                client_id=_unique_client_id("probe-17-4-1"),
                clean_start=True,
            )
            assert_connack(probe_connack, reason_code=0x00, session_present=False)

        return True, "17.4.1 completed 100 rapid connect/disconnect cycles; broker still reachable"
    except Exception as error:
        return False, f"17.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_17_4_2_multiple_clients_connect_simultaneously(config) -> tuple[bool, str]:
    process = None

    def _connect_once(client_number: int) -> tuple[bool, str]:
        try:
            with MqttClient(timeout_seconds=config.timeout_seconds) as client:
                connack = client.connect(
                    host,
                    port,
                    client_id=_unique_client_id(f"simul-17-4-2-{client_number}"),
                    clean_start=True,
                )
                assert_connack(connack, reason_code=0x00, session_present=False)
            return True, "ok"
        except Exception as error:
            return False, str(error)

    try:
        host, port, process = _start_isolated_broker()
        tcp_results = flood_connections(host, port, count=20, timeout_seconds=min(1.0, config.timeout_seconds))
        if not all(tcp_results):
            return False, f"17.4.2 TCP pre-check failed: {tcp_results.count(False)} of 20 connections failed"

        success_count = 0
        failure_messages: list[str] = []
        with ThreadPoolExecutor(max_workers=20) as executor:
            futures = [executor.submit(_connect_once, index) for index in range(20)]
            for future in futures:
                successful, details = future.result(timeout=max(5.0, config.timeout_seconds * 2))
                if successful:
                    success_count += 1
                else:
                    failure_messages.append(details)

        if failure_messages:
            return False, (
                f"17.4.2 only {success_count}/20 simultaneous clients connected; "
                f"sample failure: {failure_messages[0]}"
            )

        return True, "17.4.2 all 20 simultaneous clients connected successfully"
    except Exception as error:
        return False, f"17.4.2 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "publish/fan_out_one_publisher_ten_subscribers",
        "description": "17.1.1 1 publisher, 10 subscribers on same topic -> all 10 receive message",
        "run": run_17_1_1_one_publisher_ten_subscribers_receive,
    },
    {
        "name": "publish/fan_out_mixed_qos_subscribers_receive_subscribed_qos",
        "description": "17.1.2 1 publisher, subscribers with mixed QoS -> each received at subscribed QoS",
        "run": run_17_1_2_mixed_qos_each_subscriber_gets_subscribed_qos,
    },
    {
        "name": "publish/fan_in_ten_publishers_one_subscriber",
        "description": "17.2.1 10 publishers to same topic, 1 subscriber -> subscriber receives all 10 messages",
        "run": run_17_2_1_ten_publishers_one_subscriber_receives_all,
    },
    {
        "name": "publish/cross_traffic_different_topics_no_contamination",
        "description": "17.3.1 Multiple publishers and subscribers on different topics -> correct routing, no cross-contamination",
        "run": run_17_3_1_cross_traffic_routes_without_contamination,
    },
    {
        "name": "publish/client_publisher_and_subscriber_same_topic_receives_own_messages",
        "description": "17.3.2 Client both publisher and subscriber on same topic -> receives own messages (unless No Local)",
        "run": run_17_3_2_client_publishes_and_receives_own_message,
    },
    {
        "name": "publish/rapid_connect_disconnect_hundred_times",
        "description": "17.4.1 Client connects and disconnects rapidly 100 times -> broker stable, no leaks",
        "run": run_17_4_1_rapid_connect_disconnect_hundred_times,
    },
    {
        "name": "publish/multiple_clients_connect_simultaneously",
        "description": "17.4.2 Multiple clients connect simultaneously -> all handled correctly",
        "run": run_17_4_2_multiple_clients_connect_simultaneously,
    },
]
