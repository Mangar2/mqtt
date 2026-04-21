"""Integration tests for connection lifecycle section 1.3 (CONNACK server capabilities)."""

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
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient


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
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _connack_property(connack_result, property_name: str):
    properties = getattr(connack_result, "properties", None)
    if properties is None:
        return None
    return getattr(properties, property_name, None)


def _run_connack_property_presence_test(
    config,
    *,
    case_label: str,
    property_name: str,
) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("connack-cap"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            capability_value = _connack_property(connack, property_name)
            if capability_value is None:
                return False, f"expected CONNACK property {property_name}, but it is missing"

        return True, f"{case_label} CONNACK contains {property_name}"
    except Exception as error:
        return False, f"{case_label} failed: {error}"
    finally:
        stop_broker(process)


def run_1_3_1_connack_contains_receive_maximum(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.1",
        property_name="ReceiveMaximum",
    )


def run_1_3_2_connack_contains_maximum_qos(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("connack-cap"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            maximum_qos = _connack_property(connack, "MaximumQoS")
            if maximum_qos is None:
                return True, (
                    "1.3.2 CONNACK omits MaximumQoS; "
                    "effective server maximum QoS is 2 per MQTT 5.0"
                )

            maximum_qos_value = int(maximum_qos)
            if maximum_qos_value not in (0, 1):
                return False, (
                    "1.3.2 failed: CONNACK MaximumQoS must be 0 or 1 when present, "
                    f"got {maximum_qos_value}"
                )

        return True, f"1.3.2 CONNACK contains MaximumQoS={maximum_qos_value}"
    except Exception as error:
        return False, f"1.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_3_3_connack_contains_retain_available(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.3",
        property_name="RetainAvailable",
    )


def run_1_3_4_connack_contains_maximum_packet_size(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.4",
        property_name="MaximumPacketSize",
    )


def run_1_3_5_connack_contains_topic_alias_maximum(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.5",
        property_name="TopicAliasMaximum",
    )


def run_1_3_6_connack_contains_wildcard_subscription_available(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.6",
        property_name="WildcardSubscriptionAvailable",
    )


def run_1_3_7_connack_contains_subscription_identifier_available(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.7",
        property_name="SubscriptionIdentifierAvailable",
    )


def run_1_3_8_connack_contains_shared_subscription_available(config) -> tuple[bool, str]:
    return _run_connack_property_presence_test(
        config,
        case_label="1.3.8",
        property_name="SharedSubscriptionAvailable",
    )


TEST_CASES = [
    {
        "name": "connect/connack_capabilities/contains_receive_maximum",
        "description": "1.3.1 CONNACK contains Receive Maximum property",
        "run": run_1_3_1_connack_contains_receive_maximum,
    },
    {
        "name": "connect/connack_capabilities/contains_maximum_qos",
        "description": "1.3.2 CONNACK contains Maximum QoS property",
        "run": run_1_3_2_connack_contains_maximum_qos,
    },
    {
        "name": "connect/connack_capabilities/contains_retain_available",
        "description": "1.3.3 CONNACK contains Retain Available property",
        "run": run_1_3_3_connack_contains_retain_available,
    },
    {
        "name": "connect/connack_capabilities/contains_maximum_packet_size",
        "description": "1.3.4 CONNACK contains Maximum Packet Size property",
        "run": run_1_3_4_connack_contains_maximum_packet_size,
    },
    {
        "name": "connect/connack_capabilities/contains_topic_alias_maximum",
        "description": "1.3.5 CONNACK contains Topic Alias Maximum property",
        "run": run_1_3_5_connack_contains_topic_alias_maximum,
    },
    {
        "name": "connect/connack_capabilities/contains_wildcard_subscription_available",
        "description": "1.3.6 CONNACK contains Wildcard Subscription Available property",
        "run": run_1_3_6_connack_contains_wildcard_subscription_available,
    },
    {
        "name": "connect/connack_capabilities/contains_subscription_identifier_available",
        "description": "1.3.7 CONNACK contains Subscription Identifier Available property",
        "run": run_1_3_7_connack_contains_subscription_identifier_available,
    },
    {
        "name": "connect/connack_capabilities/contains_shared_subscription_available",
        "description": "1.3.8 CONNACK contains Shared Subscription Available property",
        "run": run_1_3_8_connack_contains_shared_subscription_available,
    },
]
