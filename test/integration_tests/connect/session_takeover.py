"""Integration tests for connection lifecycle section 1.5 (Session Takeover)."""

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
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


def _require_paho_properties() -> tuple[object, object]:
    if Properties is None or PacketTypes is None:
        raise RuntimeError("paho-mqtt properties API is required for session takeover tests")
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
    return f"integration/connect/session-takeover/{prefix}/{uuid.uuid4().hex}"


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


def _connect_with_persistent_session(
    client: MqttClient,
    host: str,
    port: int,
    *,
    client_id: str,
) -> None:
    connack = client.connect(
        host,
        port,
        client_id=client_id,
        clean_start=True,
        properties=_new_connect_properties(SessionExpiryInterval=120),
    )
    assert_connack(connack, reason_code=0x00, session_present=False)


def run_1_5_1_second_client_disconnects_first_with_8e(config) -> tuple[bool, str]:
    process = None
    shared_client_id = _unique_client_id("takeover-8e")

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as first_client:
            _connect_with_persistent_session(
                first_client,
                host,
                port,
                client_id=shared_client_id,
            )

            with MqttClient(timeout_seconds=config.timeout_seconds) as second_client:
                second_connack = second_client.connect(
                    host,
                    port,
                    client_id=shared_client_id,
                    clean_start=False,
                )
                assert_connack(second_connack, reason_code=0x00, session_present=True)

                first_disconnect = first_client.wait_for_disconnect(timeout=config.timeout_seconds)
                assert_reason_code(first_disconnect.reason_code, 0x8E)

        return True, "1.5.1 second client takeover disconnected first client with 0x8E"
    except Exception as error:
        return False, f"1.5.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_5_2_session_state_transferred_to_new_connection(config) -> tuple[bool, str]:
    process = None
    shared_client_id = _unique_client_id("takeover-state")
    topic = _unique_topic("state")

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as first_client:
            _connect_with_persistent_session(
                first_client,
                host,
                port,
                client_id=shared_client_id,
            )

            suback_codes = first_client.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for pre-takeover subscription is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as second_client:
                second_connack = second_client.connect(
                    host,
                    port,
                    client_id=shared_client_id,
                    clean_start=False,
                )
                assert_connack(second_connack, reason_code=0x00, session_present=True)

                # Unsubscribe should succeed without resubscribe if session state transferred.
                unsuback_codes = second_client.unsubscribe(topic)
                if not unsuback_codes:
                    return False, "UNSUBACK for transferred subscription is empty"
                assert_reason_code(unsuback_codes[0], 0x00)

        return True, "1.5.2 session state transferred to takeover connection"
    except Exception as error:
        return False, f"1.5.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_5_3_old_subscriptions_remain_active_after_takeover(config) -> tuple[bool, str]:
    process = None
    shared_client_id = _unique_client_id("takeover-subs")
    topic = _unique_topic("subscriptions")

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as first_client:
            _connect_with_persistent_session(
                first_client,
                host,
                port,
                client_id=shared_client_id,
            )

            suback_codes = first_client.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK for takeover subscription is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as second_client:
                second_connack = second_client.connect(
                    host,
                    port,
                    client_id=shared_client_id,
                    clean_start=False,
                )
                assert_connack(second_connack, reason_code=0x00, session_present=True)

                with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                    pub_connack = publisher.connect(
                        host,
                        port,
                        client_id=_unique_client_id("takeover-publisher"),
                        clean_start=True,
                    )
                    assert_connack(pub_connack, reason_code=0x00, session_present=False)
                    publish_reason = publisher.publish(topic, b"delivered-after-takeover", qos=1)
                    assert_reason_code(publish_reason, 0x00)

                messages = second_client.collect_messages(count=1, timeout=config.timeout_seconds)
                assert_message(
                    messages[0],
                    topic=topic,
                    payload=b"delivered-after-takeover",
                    qos=1,
                    retain=False,
                )

        return True, "1.5.3 old connection subscriptions stayed active after takeover"
    except Exception as error:
        return False, f"1.5.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/session_takeover/second_client_disconnects_first_with_8e",
        "description": "1.5.1 Second client with same Client ID disconnects first with 0x8E",
        "run": run_1_5_1_second_client_disconnects_first_with_8e,
    },
    {
        "name": "connect/session_takeover/session_state_transferred_to_new_connection",
        "description": "1.5.2 Session state is transferred to new connection",
        "run": run_1_5_2_session_state_transferred_to_new_connection,
    },
    {
        "name": "connect/session_takeover/old_subscriptions_remain_active_after_takeover",
        "description": "1.5.3 Old connection subscriptions remain active after takeover",
        "run": run_1_5_3_old_subscriptions_remain_active_after_takeover,
    },
]