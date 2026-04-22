"""Prerequisite smoke tests for integration test toolbox helpers."""

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
_raw_tcp_module = _load_helper("raw_tcp")

assert_message = _assertions_module.assert_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
send_bytes = _raw_tcp_module.send_bytes


def _unique_topic(suffix: str) -> str:
    return f"integration/prerequisites/{suffix}/{uuid.uuid4().hex}"


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_target() -> tuple[str, int, object, str]:
    overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    process = start_broker(overrides)
    return (
        _broker_module.resolve_target_host("127.0.0.1"),
        int(overrides["network.mqtt_port"]),
        process,
        "isolated broker",
    )


def run_paho_available(_config) -> tuple[bool, str]:
    try:
        import paho.mqtt.client as mqtt  # pylint: disable=import-outside-toplevel

        version_text = getattr(mqtt, "__version__", "unknown")
        return True, f"0.5.1 paho-mqtt import succeeded (version={version_text})"
    except Exception as error:  # pragma: no cover - failure path only
        return False, f"paho-mqtt import failed: {error}"


def run_mqtt_client_connect_disconnect(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process, target_label = _start_isolated_target()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            result = client.connect(host, port, client_id=_unique_client_id("pre-connect"), clean_start=True)
            assert_reason_code(result.reason_code, 0x00)
        return True, f"0.5.2 MqttClient connect/disconnect successful ({target_label})"
    except Exception as error:
        return False, f"MqttClient connect/disconnect failed: {error}"
    finally:
        stop_broker(process)


def run_pub_sub_qos0_roundtrip(config) -> tuple[bool, str]:
    topic = _unique_topic("qos0")
    payload = b"smoke-qos0"
    process = None

    try:
        host, port, process, target_label = _start_isolated_target()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            subscriber.connect(host, port, client_id=_unique_client_id("pre-sub-q0"), clean_start=True)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "SUBACK for QoS0 subscription is empty"

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                publisher.connect(host, port, client_id=_unique_client_id("pre-pub-q0"), clean_start=True)
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=0, retain=False)
        return True, f"0.5.3 QoS0 roundtrip successful ({target_label})"
    except Exception as error:
        return False, f"QoS0 roundtrip failed: {error}"
    finally:
        stop_broker(process)


def run_pub_sub_qos1_roundtrip(config) -> tuple[bool, str]:
    topic = _unique_topic("qos1")
    payload = b"smoke-qos1"
    process = None

    try:
        host, port, process, target_label = _start_isolated_target()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            subscriber.connect(host, port, client_id=_unique_client_id("pre-sub-q1"), clean_start=True)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for QoS1 subscription is empty"

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                publisher.connect(host, port, client_id=_unique_client_id("pre-pub-q1"), clean_start=True)
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=1, retain=False)
        return True, f"0.5.4 QoS1 roundtrip successful ({target_label})"
    except Exception as error:
        return False, f"QoS1 roundtrip failed: {error}"
    finally:
        stop_broker(process)


def run_pub_sub_qos2_roundtrip(config) -> tuple[bool, str]:
    topic = _unique_topic("qos2")
    payload = b"smoke-qos2"
    process = None

    try:
        host, port, process, target_label = _start_isolated_target()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            subscriber.connect(host, port, client_id=_unique_client_id("pre-sub-q2"), clean_start=True)
            suback_codes = subscriber.subscribe(topic, qos=2)
            if not suback_codes:
                return False, "SUBACK for QoS2 subscription is empty"

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                publisher.connect(host, port, client_id=_unique_client_id("pre-pub-q2"), clean_start=True)
                publish_reason = publisher.publish(topic, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=2, retain=False)
        return True, f"0.5.5 QoS2 roundtrip successful ({target_label})"
    except Exception as error:
        return False, f"QoS2 roundtrip failed: {error}"
    finally:
        stop_broker(process)


def run_raw_tcp_send_receive(config) -> tuple[bool, str]:
    try:
        client_id = f"raw-smoke-{uuid.uuid4().hex[:10]}"
        payload = encode_utf8_string(client_id)
        connect_packet = build_connect_packet(payload=payload)
        response = send_bytes(config.host, config.port, connect_packet, timeout_seconds=config.timeout_seconds)
        if not response:
            return False, "raw_tcp.send_bytes returned no response"
        if response[0] != 0x20:
            return False, f"expected CONNACK first byte 0x20, got 0x{response[0]:02X}"
        return True, f"0.5.6 raw_tcp.send_bytes received {len(response)} byte(s)"
    except Exception as error:
        return False, f"raw_tcp send/receive failed: {error}"


def run_assertions_error_message(_config) -> tuple[bool, str]:
    try:
        assert_reason_code(0x01, 0x00)
        return False, "assert_reason_code did not raise for mismatching reason codes"
    except AssertionError as error:
        message = str(error)
        if "expected 0x00" not in message or "got 0x01" not in message:
            return False, f"unexpected assertion message: {message}"
        return True, f"0.5.7 assertion message is readable: {message}"


TEST_CASES = [
    {
        "name": "prerequisites/paho_import_available",
        "description": "0.5.1 paho-mqtt library import works",
        "run": run_paho_available,
    },
    {
        "name": "prerequisites/mqtt_client_connect_disconnect",
        "description": "0.5.2 MqttClient can connect and disconnect cleanly",
        "run": run_mqtt_client_connect_disconnect,
    },
    {
        "name": "prerequisites/pub_sub_qos0_roundtrip",
        "description": "0.5.3 MqttClient QoS0 pub/sub roundtrip works",
        "run": run_pub_sub_qos0_roundtrip,
    },
    {
        "name": "prerequisites/pub_sub_qos1_roundtrip",
        "description": "0.5.4 MqttClient QoS1 pub/sub roundtrip works",
        "run": run_pub_sub_qos1_roundtrip,
    },
    {
        "name": "prerequisites/pub_sub_qos2_roundtrip",
        "description": "0.5.5 MqttClient QoS2 pub/sub roundtrip works",
        "run": run_pub_sub_qos2_roundtrip,
    },
    {
        "name": "prerequisites/raw_tcp_send_receive",
        "description": "0.5.6 raw_tcp helper can send bytes and receive broker response",
        "run": run_raw_tcp_send_receive,
    },
    {
        "name": "prerequisites/assertions_readable_error",
        "description": "0.5.7 assertions helper emits readable error messages",
        "run": run_assertions_error_message,
    },
]
