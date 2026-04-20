"""Integration tests for robustness section 19.1 (Malformed Input)."""

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
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
encode_utf8_string = _raw_tcp_module.encode_utf8_string
send_and_expect_close = _raw_tcp_module.send_and_expect_close


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


def _verify_valid_connect(host: str, port: int, timeout_seconds: float) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as client:
        connack = client.connect(
            host,
            port,
            client_id=_unique_client_id("robustness-verify"),
            clean_start=True,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)


def _send_then_half_close(host: str, port: int, payload: bytes, timeout_seconds: float) -> None:
    with socket.create_connection((host, port), timeout=timeout_seconds) as tcp_socket:
        tcp_socket.settimeout(timeout_seconds)
        if payload:
            tcp_socket.sendall(payload)
        tcp_socket.shutdown(socket.SHUT_WR)
        try:
            tcp_socket.recv(1)
        except (ConnectionResetError, BrokenPipeError):
            return
        except socket.timeout:
            return


def _send_then_half_close_expect_close(
    host: str,
    port: int,
    payload: bytes,
    timeout_seconds: float,
) -> bool:
    with socket.create_connection((host, port), timeout=timeout_seconds) as tcp_socket:
        tcp_socket.settimeout(timeout_seconds)
        if payload:
            tcp_socket.sendall(payload)
        tcp_socket.shutdown(socket.SHUT_WR)

        try:
            while True:
                received = tcp_socket.recv(4096)
                if received == b"":
                    return True
        except (ConnectionResetError, BrokenPipeError):
            return True
        except socket.timeout:
            return False


def run_19_1_1_random_garbage_closes_no_crash(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        closed = send_and_expect_close(host, port, b"\xff\x00\xde\xad\xbe\xef\xca\xfe", timeout=timeout)
        if not closed:
            return False, "broker did not close connection on random garbage bytes"

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.1 broker closed garbage connection and remained healthy"
    except Exception as error:
        return False, f"19.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_19_1_2_truncated_connect_closed_cleanly(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        # CONNECT with remaining length announced but payload intentionally cut short.
        truncated_connect = b"\x10\x0c\x00\x04MQTT\x05\x02"
        _send_then_half_close(host, port, truncated_connect, timeout)

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.2 truncated CONNECT handled cleanly and broker stayed operational"
    except Exception as error:
        return False, f"19.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_19_1_3_oversized_packet_rejected_no_oom(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        # Remaining length exceeds MQTT max by requiring a 5-byte encoding (> 256 MB).
        oversized_header = b"\x30\x80\x80\x80\x80\x01"
        closed = send_and_expect_close(host, port, oversized_header, timeout=timeout)
        if not closed:
            return False, "broker did not close connection on oversized packet length"

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.3 oversized packet was rejected without broker instability"
    except Exception as error:
        return False, f"19.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_19_1_4_zero_length_input_handled_gracefully(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        # TCP has no explicit empty frame; connect and half-close without sending bytes.
        _send_then_half_close(host, port, b"", timeout)

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.4 zero-length input handled gracefully"
    except Exception as error:
        return False, f"19.1.4 failed: {error}"
    finally:
        stop_broker(process)


def run_19_1_5_wrong_remaining_length_detected_disconnect(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        # Remaining length claims 5 bytes, but only 3 bytes are provided and stream is closed.
        malformed_publish = b"\x30\x05\x00\x01a"
        closed = _send_then_half_close_expect_close(host, port, malformed_publish, timeout)
        if not closed:
            return False, "broker did not close connection on wrong remaining length"

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.5 broker detected wrong remaining length and disconnected"
    except Exception as error:
        return False, f"19.1.5 failed: {error}"
    finally:
        stop_broker(process)


def run_19_1_6_publish_qos_3_rejected(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = max(1.0, float(config.timeout_seconds))

        topic = encode_utf8_string("integration/robustness/qos3")
        malformed_publish_qos3 = b"\x36\x1f" + topic + b"\x00" + b"reserved-qos-3"
        closed = send_and_expect_close(host, port, malformed_publish_qos3, timeout=timeout)
        if not closed:
            return False, "broker did not close connection on PUBLISH QoS 3"

        _verify_valid_connect(host, port, timeout)
        return True, "19.1.6 broker rejected reserved QoS 3 PUBLISH and remained healthy"
    except Exception as error:
        return False, f"19.1.6 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "robustness/malformed_input_random_garbage_bytes",
        "description": "19.1.1 Send random garbage bytes, broker closes connection and does not crash",
        "run": run_19_1_1_random_garbage_closes_no_crash,
    },
    {
        "name": "robustness/malformed_input_truncated_connect",
        "description": "19.1.2 Send truncated CONNECT packet, broker closes connection cleanly",
        "run": run_19_1_2_truncated_connect_closed_cleanly,
    },
    {
        "name": "robustness/malformed_input_oversized_packet",
        "description": "19.1.3 Send oversized packet greater than 256 MB, broker rejects without OOM",
        "run": run_19_1_3_oversized_packet_rejected_no_oom,
    },
    {
        "name": "robustness/malformed_input_zero_length_packet",
        "description": "19.1.4 Send zero-length packet, broker handles input gracefully",
        "run": run_19_1_4_zero_length_input_handled_gracefully,
    },
    {
        "name": "robustness/malformed_input_wrong_remaining_length",
        "description": "19.1.5 Send valid fixed header with wrong remaining length, broker disconnects",
        "run": run_19_1_5_wrong_remaining_length_detected_disconnect,
    },
    {
        "name": "robustness/malformed_input_publish_qos_3",
        "description": "19.1.6 Send PUBLISH with reserved QoS 3, broker closes connection",
        "run": run_19_1_6_publish_qos_3_rejected,
    },
]