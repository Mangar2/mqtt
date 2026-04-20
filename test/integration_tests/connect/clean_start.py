"""Integration tests for connection lifecycle section 1.4 (Clean Start)."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import socket
import uuid

try:
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties
except Exception:
    PacketTypes = None
    Properties = None


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


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for Clean Start tests")
    return Properties, PacketTypes


def _new_connect_properties(**values):
    properties_cls, packet_types = _require_paho_properties()
    props = properties_cls(packet_types.CONNECT)
    for key, value in values.items():
        setattr(props, key, value)
    return props


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/connect/clean-start/{prefix}/{uuid.uuid4().hex}"


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


def _create_persisted_session(
    host: str,
    port: int,
    timeout_seconds: float,
    *,
    client_id: str,
    topic: str,
) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as session_client:
        connack = session_client.connect(
            host,
            port,
            client_id=client_id,
            clean_start=True,
            properties=_new_connect_properties(SessionExpiryInterval=120),
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        suback_codes = session_client.subscribe(topic, qos=1)
        if not suback_codes:
            raise AssertionError("SUBACK for session setup is empty")
        assert_reason_code(suback_codes[0], 0x01)


def run_1_4_1_clean_start_true_new_session(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("clean-start-true"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
        return True, "1.4.1 Clean Start=1 created a new session with Session Present=0"
    except Exception as error:
        return False, f"1.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_4_2_clean_start_false_without_prior_session(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("clean-start-false-new"),
                clean_start=False,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
        return True, "1.4.2 Clean Start=0 without prior session returned Session Present=0"
    except Exception as error:
        return False, f"1.4.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_4_3_clean_start_false_with_prior_session_resumes(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("clean-start-resume")
    topic = _unique_topic("resume")

    try:
        host, port, process = _start_isolated_broker()
        _create_persisted_session(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("clean-start-resume-pub"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"queued-for-resume", qos=1)
            assert_reason_code(publish_reason, 0x00)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resume_client:
            resumed_connack = resume_client.connect(
                host,
                port,
                client_id=client_id,
                clean_start=False,
            )
            assert_connack(resumed_connack, reason_code=0x00, session_present=True)

            messages = resume_client.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"queued-for-resume", qos=1, retain=False)

        return True, "1.4.3 Clean Start=0 resumed prior session with Session Present=1"
    except Exception as error:
        return False, f"1.4.3 failed: {error}"
    finally:
        stop_broker(process)


def run_1_4_4_clean_start_true_discards_prior_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("clean-start-discard")
    topic = _unique_topic("discard")

    try:
        host, port, process = _start_isolated_broker()
        _create_persisted_session(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("clean-start-discard-pub"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"queued-before-discard", qos=1)
            assert_reason_code(publish_reason, 0x00)

        with MqttClient(timeout_seconds=config.timeout_seconds) as clean_start_client:
            fresh_connack = clean_start_client.connect(
                host,
                port,
                client_id=client_id,
                clean_start=True,
            )
            assert_connack(fresh_connack, reason_code=0x00, session_present=False)
            assert_no_message(clean_start_client, timeout=min(1.5, config.timeout_seconds))

        return True, "1.4.4 Clean Start=1 discarded prior session and queued data"
    except Exception as error:
        return False, f"1.4.4 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/clean_start/new_session_clean_start_true",
        "description": "1.4.1 Clean Start = 1 creates new session (Session Present = 0)",
        "run": run_1_4_1_clean_start_true_new_session,
    },
    {
        "name": "connect/clean_start/new_session_clean_start_false_without_prior",
        "description": "1.4.2 Clean Start = 0 without prior session yields Session Present = 0",
        "run": run_1_4_2_clean_start_false_without_prior_session,
    },
    {
        "name": "connect/clean_start/resume_session_clean_start_false_with_prior",
        "description": "1.4.3 Clean Start = 0 with prior session resumes session (Session Present = 1)",
        "run": run_1_4_3_clean_start_false_with_prior_session_resumes,
    },
    {
        "name": "connect/clean_start/discard_session_clean_start_true_with_prior",
        "description": "1.4.4 Clean Start = 1 with prior session discards old session (Session Present = 0)",
        "run": run_1_4_4_clean_start_true_discards_prior_session,
    },
]