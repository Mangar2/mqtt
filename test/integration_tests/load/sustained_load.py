"""Integration tests for load section 18.4 (sustained load)."""

from __future__ import annotations

from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import time
import uuid


_SESSION_EXPIRY_SECONDS = 300
_WRITE_QUEUE_BYTES_HIGH = 4 * 1024 * 1024
_MAX_QUEUED_MESSAGES_HIGH = 1000
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"


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
        effective_overrides["broker.max_queued_messages"] = _MAX_QUEUED_MESSAGES_HIGH
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _connect_client(
    host: str,
    port: int,
    timeout_seconds: float,
    prefix: str,
    *,
    clean_start: bool = True,
    client_id: str = "",
    connect_properties=None,
    expected_session_present: bool = False,
) -> MqttClient:
    client = MqttClient(timeout_seconds=timeout_seconds)
    connack = client.connect(
        host,
        port,
        client_id=client_id or _unique_client_id(prefix),
        clean_start=clean_start,
        properties=connect_properties,
    )
    assert_connack(connack, reason_code=0x00, session_present=expected_session_present)
    return client


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _read_process_rss_kb(pid: int) -> int | None:
    try:
        result = subprocess.run(
            ["ps", "-o", "rss=", "-p", str(pid)],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            return None
        value_text = result.stdout.strip()
        if value_text == "":
            return None
        return int(value_text)
    except Exception:
        return None


def run_18_4_1_fifty_clients_continuous_pub_sub_sixty_seconds(config) -> tuple[bool, str]:
    process = None
    client_count = 50
    stabilization_seconds = 60.0
    observation_seconds = 60.0
    topic_root = f"integration/load/sustained/18-4-1/{uuid.uuid4().hex}"

    try:
        host, port, process = _start_isolated_broker()
        with ExitStack() as stack:
            clients = [
                stack.enter_context(_connect_client(host, port, config.timeout_seconds, f"load-18-4-1-{index}"))
                for index in range(client_count)
            ]

            topics = [f"{topic_root}/client-{index}" for index in range(client_count)]
            for index, client in enumerate(clients):
                suback_codes = client.subscribe(topics[index], qos=0)
                if not suback_codes:
                    return False, f"18.4.1 empty SUBACK for client index {index}"
                assert_reason_code(suback_codes[0], 0x00)

            publish_counter = 0
            phase_end_time = time.monotonic() + stabilization_seconds
            while time.monotonic() < phase_end_time:
                for index, client in enumerate(clients):
                    target_index = (index + 1) % client_count
                    payload = f"18.4.1-{publish_counter}".encode("utf-8")
                    assert_reason_code(client.publish(topics[target_index], payload, qos=0), 0x00)
                    publish_counter += 1

                for index, client in enumerate(clients):
                    messages = client.collect_messages(count=1, timeout=max(config.timeout_seconds, 8.0))
                    if len(messages) != 1:
                        return False, f"18.4.1 client {index} did not receive expected traffic"

                if process is None or process.poll() is not None:
                    return False, "18.4.1 broker crashed during sustained load"

            stabilized_rss_kb = _read_process_rss_kb(process.pid) if process is not None else None
            if stabilized_rss_kb is None:
                return False, "18.4.1 unable to read broker memory after stabilization phase"

            phase_end_time = time.monotonic() + observation_seconds
            while time.monotonic() < phase_end_time:
                for index, client in enumerate(clients):
                    target_index = (index + 1) % client_count
                    payload = f"18.4.1-{publish_counter}".encode("utf-8")
                    assert_reason_code(client.publish(topics[target_index], payload, qos=0), 0x00)
                    publish_counter += 1

                for index, client in enumerate(clients):
                    messages = client.collect_messages(count=1, timeout=max(config.timeout_seconds, 8.0))
                    if len(messages) != 1:
                        return False, f"18.4.1 client {index} did not receive expected traffic"

                if process is None or process.poll() is not None:
                    return False, "18.4.1 broker crashed during sustained load"

        if process is None or process.poll() is not None:
            return False, "18.4.1 broker is not alive after sustained load"

        final_rss_kb = _read_process_rss_kb(process.pid)
        if final_rss_kb is None:
            return False, "18.4.1 unable to read broker memory after observation phase"
        if final_rss_kb > stabilized_rss_kb:
            return (
                False,
                "18.4.1 memory growth detected after stabilization: "
                f"stabilized={stabilized_rss_kb}KB final={final_rss_kb}KB",
            )

        return (
            True,
            f"18.4.1 sustained load completed for 120s (60s stabilization + 60s observation), publishes={publish_counter}",
        )
    except Exception as error:
        return False, f"18.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_18_4_2_retained_store_thousand_entries_returns_correct_retained(config) -> tuple[bool, str]:
    process = None
    entry_count = 1000
    topic_root = f"integration/load/sustained/18-4-2/{uuid.uuid4().hex}"

    try:
        host, port, process = _start_isolated_broker()
        heavy_timeout_seconds = max(config.timeout_seconds, 30.0)
        with _connect_client(
            host,
            port,
            heavy_timeout_seconds,
            "pub-18-4-2",
            expected_session_present=False,
        ) as publisher:
            expected: dict[str, bytes] = {}
            for index in range(entry_count):
                topic = f"{topic_root}/{index}"
                payload = f"retained-18.4.2-{index}".encode("utf-8")
                expected[topic] = payload
                assert_reason_code(publisher.publish(topic, payload, qos=0, retain=True), 0x00)

        with _connect_client(
            host,
            port,
            heavy_timeout_seconds,
            "sub-18-4-2",
            expected_session_present=False,
        ) as subscriber:
            suback_codes = subscriber.subscribe(f"{topic_root}/#", qos=0)
            if not suback_codes:
                return False, "18.4.2 empty SUBACK for retained catch-all subscription"
            assert_reason_code(suback_codes[0], 0x00)

            messages = subscriber.collect_messages(
                count=entry_count,
                timeout=max(config.timeout_seconds * 4.0, 15.0),
            )
            delivered = {message.topic: bytes(message.payload) for message in messages}
            if delivered != expected:
                missing = len(set(expected.keys()) - set(delivered.keys()))
                mismatched_payloads = 0
                for topic, expected_payload in expected.items():
                    if topic in delivered and delivered[topic] != expected_payload:
                        mismatched_payloads += 1
                return (
                    False,
                    "18.4.2 retained replay mismatch: "
                    f"missing={missing}, payload_mismatches={mismatched_payloads}",
                )

        return True, "18.4.2 retained store with 1000 entries returned complete retained replay"
    except Exception as error:
        return False, f"18.4.2 failed: {error}"
    finally:
        stop_broker(process)


def run_18_4_3_offline_queue_five_hundred_messages_delivered_on_reconnect(config) -> tuple[bool, str]:
    process = None
    queue_count = 500
    topic = f"integration/load/sustained/18-4-3/{uuid.uuid4().hex}"
    client_id = _unique_client_id("offline-18-4-3")

    try:
        host, port, process = _start_isolated_broker()

        connect_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)
        with _connect_client(
            host,
            port,
            config.timeout_seconds,
            "sub-18-4-3-initial",
            clean_start=True,
            client_id=client_id,
            connect_properties=connect_properties,
            expected_session_present=False,
        ) as subscriber:
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "18.4.3 empty SUBACK when creating offline queue subscription"
            assert_reason_code(suback_codes[0], 0x01)

        with _connect_client(
            host,
            port,
            config.timeout_seconds,
            "pub-18-4-3",
            expected_session_present=False,
        ) as publisher:
            for index in range(queue_count):
                payload = f"offline-18.4.3-{index}".encode("utf-8")
                reason = int(publisher.publish(topic, payload, qos=1))
                if reason not in (0x00, 0x10):
                    return False, f"18.4.3 unexpected PUBACK reason 0x{reason:02X} at message {index}"

        reconnect_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)
        with _connect_client(
            host,
            port,
            config.timeout_seconds,
            "sub-18-4-3-resume",
            clean_start=False,
            client_id=client_id,
            connect_properties=reconnect_properties,
            expected_session_present=True,
        ) as resumed:
            messages = resumed.collect_messages(
                count=queue_count,
                timeout=max(config.timeout_seconds * 8.0, 30.0),
            )
            delivered_payloads = {bytes(message.payload) for message in messages}
            expected_payloads = {f"offline-18.4.3-{index}".encode("utf-8") for index in range(queue_count)}
            if delivered_payloads != expected_payloads:
                missing = len(expected_payloads - delivered_payloads)
                return False, f"18.4.3 missing {missing} queued payload(s) on reconnect"

        return True, "18.4.3 delivered all 500 queued messages after reconnect"
    except Exception as error:
        return False, f"18.4.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "load/sustained_load_fifty_clients_sixty_seconds",
        "description": "18.4.1 50 clients continuous pub/sub with 60s stabilization + 60s memory observation -> no crash, no memory growth",
        "run": run_18_4_1_fifty_clients_continuous_pub_sub_sixty_seconds,
    },
    {
        "name": "load/sustained_load_retained_store_thousand_entries",
        "description": "18.4.2 Retained message store with 1000 entries -> subscribe returns correct retained messages",
        "run": run_18_4_2_retained_store_thousand_entries_returns_correct_retained,
    },
    {
        "name": "load/sustained_load_offline_queue_five_hundred",
        "description": "18.4.3 Offline queue with 500 queued messages -> all delivered on reconnect",
        "run": run_18_4_3_offline_queue_five_hundred_messages_delivered_on_reconnect,
    },
]
