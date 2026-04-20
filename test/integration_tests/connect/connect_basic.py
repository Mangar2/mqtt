"""Integration tests for connection lifecycle section 1.1 (CONNECT basic)."""

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

assert_connack = _assertions_module.assert_connack
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
build_packet = _raw_tcp_module.build_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
send_and_expect_close = _raw_tcp_module.send_and_expect_close
send_bytes = _raw_tcp_module.send_bytes


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/connect/basic/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker(overrides: dict[str, object] | None = None):
    effective_overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
    }
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _assigned_client_identifier(connack_result) -> str | None:
    properties = getattr(connack_result, "properties", None)
    if properties is None:
        return None

    value = getattr(properties, "AssignedClientIdentifier", None)
    if isinstance(value, str) and value:
        return value
    return None


def run_1_1_1_anonymous_connect_success(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("anonymous"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
        return True, "1.1.1 anonymous CONNECT accepted"
    except Exception as error:
        return False, f"1.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_2_valid_username_password_success(config) -> tuple[bool, str]:
    process = None
    username = "integration-user"
    password = "integration-pass"

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{username}:{password}",
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("auth-ok"),
                clean_start=True,
                username=username,
                password=password,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
        return True, "1.1.2 valid username/password accepted"
    except Exception as error:
        return False, f"1.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_3_invalid_username_password_rejected(config) -> tuple[bool, str]:
    process = None
    username = "integration-user"
    password = "integration-pass"

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{username}:{password}",
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("auth-bad"),
                clean_start=True,
                username=username,
                password="wrong-password",
            )
            assert_connack(connack, reason_code=0x86, session_present=False)
        return True, "1.1.3 invalid password rejected with 0x86"
    except Exception as error:
        return False, f"1.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_4_empty_client_id_assigned(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id="",
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            assigned_id = _assigned_client_identifier(connack)
            if assigned_id is None:
                return False, "expected Assigned Client Identifier in CONNACK"
        return True, "1.1.4 empty Client ID assigned by broker"
    except Exception as error:
        return False, f"1.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_5_explicit_client_id_used(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("explicit")
    topic = _unique_topic("explicit-client-id")

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(
                host,
                port,
                client_id=_unique_client_id("sub"),
                clean_start=True,
            )
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "expected non-empty SUBACK codes for subscriber"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=client_id,
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                if _assigned_client_identifier(pub_connack) is not None:
                    return False, "broker should not assign a new client identifier for explicit Client ID"

                publish_result = publisher.publish(topic, b"explicit-id-ok", qos=0)
                assert_reason_code(publish_result, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            if messages[0].topic != topic or messages[0].payload != b"explicit-id-ok":
                return False, "subscriber did not receive expected publish from explicit Client ID"

        return True, "1.1.5 explicit Client ID accepted and used in active session"
    except Exception as error:
        return False, f"1.1.5 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_6_invalid_protocol_version_refused(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )

        payload = encode_utf8_string(_unique_client_id("proto-ver"))
        invalid_connect = build_connect_packet(protocol_version=0x03, payload=payload)
        response = send_bytes(host, port, invalid_connect, timeout_seconds=config.timeout_seconds)

        if not response:
            return True, "1.1.6 invalid protocol version was refused by closing connection"

        if response[0] != 0x20:
            return False, f"expected CONNACK(0x20) or close, got first byte 0x{response[0]:02X}"

        if len(response) < 4:
            return False, f"expected full CONNACK for refusal, got {len(response)} byte(s)"

        reason_code = int(response[3])
        if reason_code == 0x00:
            return False, "invalid protocol version must not be accepted (received Success)"

        return True, f"1.1.6 invalid protocol version refused with reason 0x{reason_code:02X}"
    except Exception as error:
        return False, f"1.1.6 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_7_invalid_protocol_name_closed(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )
        payload = encode_utf8_string(_unique_client_id("proto-name"))
        invalid_connect = build_connect_packet(protocol_name="MQTs", payload=payload)
        closed = send_and_expect_close(host, port, invalid_connect, timeout=config.timeout_seconds)
        if not closed:
            return False, "broker did not close TCP connection for invalid protocol name"
        return True, "1.1.7 invalid protocol name caused connection close"
    except Exception as error:
        return False, f"1.1.7 failed: {error}"
    finally:
        stop_broker(process)


def run_1_1_8_reserved_connect_flags_closed(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
            }
        )

        payload = encode_utf8_string(_unique_client_id("reserved-header"))
        valid_connect = build_connect_packet(payload=payload)
        malformed_connect = bytes([0x11]) + valid_connect[1:]

        closed = send_and_expect_close(
            host,
            port,
            malformed_connect,
            timeout=config.timeout_seconds,
        )
        if not closed:
            return False, "broker did not close TCP connection for CONNECT with reserved fixed-header flags"
        return True, "1.1.8 reserved fixed-header flags caused connection close"
    except Exception as error:
        return False, f"1.1.8 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/basic/anonymous_connect_success",
        "description": "1.1.1 Anonymous connect returns CONNACK success",
        "run": run_1_1_1_anonymous_connect_success,
    },
    {
        "name": "connect/basic/valid_username_password_success",
        "description": "1.1.2 Valid username/password returns CONNACK success",
        "run": run_1_1_2_valid_username_password_success,
    },
    {
        "name": "connect/basic/invalid_username_password_rejected",
        "description": "1.1.3 Invalid username/password returns CONNACK 0x86",
        "run": run_1_1_3_invalid_username_password_rejected,
    },
    {
        "name": "connect/basic/empty_client_id_assigned",
        "description": "1.1.4 Empty Client ID gets Assigned Client Identifier",
        "run": run_1_1_4_empty_client_id_assigned,
    },
    {
        "name": "connect/basic/explicit_client_id_used",
        "description": "1.1.5 Explicit Client ID is accepted and session is usable",
        "run": run_1_1_5_explicit_client_id_used,
    },
    {
        "name": "connect/basic/invalid_protocol_version_refused",
        "description": "1.1.6 Invalid protocol version is refused",
        "run": run_1_1_6_invalid_protocol_version_refused,
    },
    {
        "name": "connect/basic/invalid_protocol_name_closed",
        "description": "1.1.7 Invalid protocol name closes connection",
        "run": run_1_1_7_invalid_protocol_name_closed,
    },
    {
        "name": "connect/basic/reserved_connect_flags_closed",
        "description": "1.1.8 Reserved CONNECT fixed-header flags close connection",
        "run": run_1_1_8_reserved_connect_flags_closed,
    },
]
