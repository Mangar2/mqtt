"""Integration tests for exact payload byte passthrough behavior."""

from __future__ import annotations

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

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/publish/payload/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker():
    overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    process = start_broker(overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(overrides["network.mqtt_port"]), process


def run_publish_payload_exact_bytes_passthrough(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("exact")
    payload = (
        b'{"token":"send-token","message":{"topic":"'
        + topic.encode("utf-8")
        + b'","value":"42","reason":[{"message":"alpha","timestamp":"2026-05-08T12:00:00Z"},{"message":"beta","timestamp":"2026-05-08T12:00:01Z"}]},"meta":{"spaces":"a  b","unicode":"ascii-only"}}'
    )

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub-payload"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            subscriber.subscribe(topic, qos=1)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-payload"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=1)
                if int(publish_reason) != 0x00:
                    return False, f"publish reason mismatch: expected 0x00, got 0x{int(publish_reason):02X}"

            message = subscriber.collect_message_for_topic(
                expected_topic=topic,
                timeout=config.timeout_seconds,
            )
            assert_message(message, topic=topic, payload=payload, qos=1, retain=False)

        return True, "publish payload bytes preserved exactly"
    except Exception as error:
        return False, f"payload passthrough failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "publish/payload_exact_bytes_passthrough",
        "description": "2.x publish payload bytes are delivered unchanged to subscribers",
        "run": run_publish_payload_exact_bytes_passthrough,
    }
]
