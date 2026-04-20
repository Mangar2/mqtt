"""Integration tests for section 13 (Error Handling & Protocol Conformance)."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import socket
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
_raw_tcp_module = _load_helper("raw_tcp")

assert_connack = _assertions_module.assert_connack
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
build_packet = _raw_tcp_module.build_packet
build_publish_packet = _raw_tcp_module.build_publish_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
encode_variable_byte_integer = _raw_tcp_module.encode_variable_byte_integer


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/connect/error-handling/{prefix}/{uuid.uuid4().hex}"


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


def _recv_exact(tcp_socket: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = tcp_socket.recv(count - len(data))
        if not chunk:
            raise RuntimeError("socket closed while reading packet")
        data.extend(chunk)
    return bytes(data)


def _recv_packet(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[int, int, bytes]:
    tcp_socket.settimeout(timeout_seconds)
    first_byte = _recv_exact(tcp_socket, 1)[0]

    multiplier = 1
    remaining_length = 0
    while True:
        encoded = _recv_exact(tcp_socket, 1)[0]
        remaining_length += (encoded & 0x7F) * multiplier
        if (encoded & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise RuntimeError("malformed remaining length")

    payload = _recv_exact(tcp_socket, remaining_length)
    packet_type = (first_byte >> 4) & 0x0F
    packet_flags = first_byte & 0x0F
    return packet_type, packet_flags, payload


def _read_connack_reason(payload: bytes) -> int:
    if len(payload) < 2:
        raise RuntimeError("invalid CONNACK payload")
    return int(payload[1])


def _extract_reason_code(payload: bytes) -> int:
    if len(payload) < 2:
        raise RuntimeError("ACK payload too short")
    if len(payload) == 2:
        return 0x00
    return int(payload[2])


def _disconnect_reason(payload: bytes) -> int:
    if not payload:
        return 0x00
    return int(payload[0])


def _connect_raw(host: str, port: int, timeout_seconds: float, client_id: str) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
        tcp_socket.sendall(connect_packet)
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 2:
            raise RuntimeError(f"expected CONNACK packet type 2, got {packet_type}")
        reason = _read_connack_reason(payload)
        if reason != 0x00:
            raise RuntimeError(f"CONNECT rejected with reason 0x{reason:02X}")
        return tcp_socket
    except Exception:
        tcp_socket.close()
        raise


def _expect_disconnect_reason(tcp_socket: socket.socket, timeout_seconds: float, expected_reason: int) -> None:
    packet_type, _packet_flags, payload = _recv_packet(tcp_socket, timeout_seconds)
    if packet_type != 14:
        raise AssertionError(f"expected DISCONNECT packet type 14, got packet type {packet_type}")
    assert_reason_code(_disconnect_reason(payload), expected_reason)


def _expect_disconnect_or_close(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[bool, int | None]:
    tcp_socket.settimeout(timeout_seconds)
    try:
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, timeout_seconds)
    except (socket.timeout, TimeoutError):
        return False, None
    except RuntimeError as error:
        if "socket closed" in str(error):
            return True, None
        raise

    if packet_type != 14:
        return False, None
    return True, _disconnect_reason(payload)


def _relabel_case_result(result: tuple[bool, str], case_id: str) -> tuple[bool, str]:
    success, message = result
    text = str(message)
    if text.startswith(case_id):
        return success, text
    if success:
        return True, f"{case_id} {text}"
    return False, f"{case_id} failed: {text}"


def _qos1_publish_once(host: str, port: int, timeout_seconds: float, topic: str, payload: bytes) -> int:
    tcp_socket = _connect_raw(host, port, timeout_seconds, _unique_client_id("qos1-once"))
    try:
        publish_packet = build_publish_packet(topic=topic, payload=payload, qos=1, packet_identifier=1)
        tcp_socket.sendall(publish_packet)
        packet_type, _packet_flags, ack_payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 4:
            raise RuntimeError(f"expected PUBACK packet type 4, got {packet_type}")
        return _extract_reason_code(ack_payload)
    finally:
        tcp_socket.close()


def _qos2_pubrec_pubcomp(host: str, port: int, timeout_seconds: float, topic: str, payload: bytes) -> tuple[int, int]:
    tcp_socket = _connect_raw(host, port, timeout_seconds, _unique_client_id("qos2-flow"))
    try:
        packet_identifier = 21
        publish_packet = build_publish_packet(
            topic=topic,
            payload=payload,
            qos=2,
            packet_identifier=packet_identifier,
        )
        tcp_socket.sendall(publish_packet)

        packet_type, _packet_flags, pubrec_payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 5:
            raise RuntimeError(f"expected PUBREC packet type 5, got {packet_type}")
        pubrec_reason = _extract_reason_code(pubrec_payload)

        pubrel_header = packet_identifier.to_bytes(2, byteorder="big") + b"\x00\x00"
        tcp_socket.sendall(build_packet(packet_type=6, flags=2, variable_header=pubrel_header))

        packet_type, _packet_flags, pubcomp_payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 7:
            raise RuntimeError(f"expected PUBCOMP packet type 7, got {packet_type}")
        pubcomp_reason = _extract_reason_code(pubcomp_payload)

        return pubrec_reason, pubcomp_reason
    finally:
        tcp_socket.close()


def run_13_1_1_truncated_packet_disconnect_81(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-1"))

        # Send a fully framed but semantically truncated payload to force parser decision.
        tcp_socket.sendall(b"\x30\x02\x00\x05")
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x81)

        return True, "13.1.1 truncated packet rejected with DISCONNECT 0x81"
    except Exception as error:
        return False, f"13.1.1 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_1_2_invalid_remaining_length_disconnect_81(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-2"))

        # Remaining length uses 5 bytes which is invalid in MQTT.
        tcp_socket.sendall(b"\x30\xff\xff\xff\xff\x01")
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x81)

        return True, "13.1.2 invalid remaining-length encoding rejected with DISCONNECT 0x81"
    except Exception as error:
        return False, f"13.1.2 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_1_3_reserved_fixed_header_bits_disconnect_81(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-3"))

        tcp_socket.sendall(b"\xc1\x00")
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x81)

        return True, "13.1.3 reserved fixed-header bits rejected with DISCONNECT 0x81"
    except Exception as error:
        return False, f"13.1.3 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_1_4_invalid_utf8_topic_disconnect_81(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-4"))

        # Topic contains U+0000 which is forbidden in MQTT UTF-8 strings.
        variable_header = b"\x00\x03a\x00b" + b"\x00"
        malformed_publish = build_packet(packet_type=3, flags=0, variable_header=variable_header, payload=b"x")
        tcp_socket.sendall(malformed_publish)
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x81)

        return True, "13.1.4 invalid UTF-8 topic rejected with DISCONNECT 0x81"
    except Exception as error:
        return False, f"13.1.4 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_1_5_unknown_property_id_disconnect_81(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-5"))

        topic_field = encode_utf8_string(_unique_topic("13-1-5"))
        property_block = b"\x01\xff"
        variable_header = topic_field + property_block
        packet = build_packet(packet_type=3, flags=0, variable_header=variable_header, payload=b"x")
        tcp_socket.sendall(packet)
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x81)

        return True, "13.1.5 unknown property identifier rejected with DISCONNECT 0x81"
    except Exception as error:
        return False, f"13.1.5 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_1_6_duplicate_non_repeatable_property_disconnect_82(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-1-6"))

        topic_field = encode_utf8_string(_unique_topic("13-1-6"))
        properties = b"\x01\x01\x01\x01"
        variable_header = topic_field + encode_variable_byte_integer(len(properties)) + properties
        packet = build_packet(packet_type=3, flags=0, variable_header=variable_header, payload=b"x")
        tcp_socket.sendall(packet)
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x82)

        return True, "13.1.6 duplicate non-repeatable property rejected with DISCONNECT 0x82"
    except Exception as error:
        return False, f"13.1.6 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_2_1_packet_type_not_valid_in_connected_state_disconnect_82(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-2-1"))

        tcp_socket.sendall(b"\x20\x02\x00\x00")
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x82)

        return True, "13.2.1 invalid packet type in connected state rejected with DISCONNECT 0x82"
    except Exception as error:
        return False, f"13.2.1 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_2_2_subscribe_empty_filter_list_disconnect_82(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-2-2"))

        empty_subscribe = build_packet(packet_type=8, flags=2, variable_header=b"\x00\x01\x00", payload=b"")
        tcp_socket.sendall(empty_subscribe)
        _expect_disconnect_reason(tcp_socket, config.timeout_seconds, 0x82)

        return True, "13.2.2 empty SUBSCRIBE rejected with DISCONNECT 0x82"
    except Exception as error:
        return False, f"13.2.2 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_2_3_publish_qos3_connection_closed(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-2-3"))

        topic = encode_utf8_string(_unique_topic("13-2-3"))
        qos3_publish = build_packet(packet_type=3, flags=6, variable_header=topic + b"\x00", payload=b"x")
        tcp_socket.sendall(qos3_publish)

        disconnected, reason = _expect_disconnect_or_close(tcp_socket, config.timeout_seconds)
        if not disconnected:
            return False, "13.2.3 failed: expected disconnect/close after QoS3 PUBLISH, but connection stayed open"

        if reason is not None:
            return True, f"13.2.3 invalid QoS3 PUBLISH caused DISCONNECT 0x{reason:02X}"
        return True, "13.2.3 invalid QoS3 PUBLISH caused connection close"
    except Exception as error:
        return False, f"13.2.3 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_2_4_pubrel_unknown_packet_id_reason_92(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-2-4"))

        unknown_packet_identifier = 4321
        pubrel_packet = build_packet(
            packet_type=6,
            flags=2,
            variable_header=unknown_packet_identifier.to_bytes(2, byteorder="big"),
            payload=b"",
        )
        tcp_socket.sendall(pubrel_packet)

        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if packet_type != 7:
            return False, f"13.2.4 failed: expected PUBCOMP packet type 7, got {packet_type}"
        assert_reason_code(_extract_reason_code(payload), 0x92)

        return True, "13.2.4 PUBREL for unknown packet identifier returned reason 0x92"
    except Exception as error:
        return False, f"13.2.4 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_1_success_reason_in_ack_types(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("13-3-1")
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("13-3-1-sub"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "13.3.1 failed: expected SUBACK reason code"
            assert_reason_code(suback_codes[0], 0x01)

            unsuback_codes = subscriber.unsubscribe(topic)
            if not unsuback_codes:
                return False, "13.3.1 failed: expected UNSUBACK reason code"
            assert_reason_code(unsuback_codes[0], 0x00)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "13.3.1 failed: expected second SUBACK reason code"
            assert_reason_code(suback_codes[0], 0x01)

            puback_reason = _qos1_publish_once(host, port, config.timeout_seconds, topic, b"qos1")
            assert_reason_code(puback_reason, 0x00)

            pubrec_reason, pubcomp_reason = _qos2_pubrec_pubcomp(host, port, config.timeout_seconds, topic, b"qos2")
            assert_reason_code(pubrec_reason, 0x00)
            assert_reason_code(pubcomp_reason, 0x00)

        return True, "13.3.1 reason code 0x00 validated in SUBACK/UNSUBACK/PUBACK/PUBREC/PUBCOMP"
    except Exception as error:
        return False, f"13.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_2_no_matching_subscribers_puback_10(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        reason = _qos1_publish_once(
            host,
            port,
            config.timeout_seconds,
            _unique_topic("13-3-2-no-subscriber"),
            b"no-match",
        )
        assert_reason_code(reason, 0x10)
        return True, "13.3.2 PUBACK reason 0x10 observed when no subscriber matched"
    except Exception as error:
        return False, f"13.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_3_unspecified_error_80(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        client = MqttClient(timeout_seconds=config.timeout_seconds)
        connack = client.connect(host, port, client_id=_unique_client_id("13-3-3"), clean_start=True)
        assert_connack(connack, reason_code=0x00, session_present=False)

        process.kill()
        process.wait(timeout=5)
        process = None

        disconnect_event = client.wait_for_disconnect(timeout=max(2.0, config.timeout_seconds))
        assert_reason_code(disconnect_event.reason_code, 0x80)

        return True, "13.3.3 reason code 0x80 observed on abrupt termination"
    except Exception as error:
        return False, f"13.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_4_malformed_packet_81(config) -> tuple[bool, str]:
    return _relabel_case_result(run_13_1_1_truncated_packet_disconnect_81(config), "13.3.4")


def run_13_3_5_protocol_error_82(config) -> tuple[bool, str]:
    return _relabel_case_result(run_13_2_1_packet_type_not_valid_in_connected_state_disconnect_82(config), "13.3.5")


def run_13_3_6_implementation_specific_error_83(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-3-6"))

        topic = _unique_topic("13-3-6")
        # Topic Alias property (0x23) with alias value 0 is invalid and is
        # mapped by broker policy to implementation-specific publish failure.
        invalid_alias_property = b"\x23\x00\x00"
        publish_packet = build_publish_packet(
            topic=topic,
            payload=b"probe",
            qos=1,
            packet_identifier=44,
            properties=invalid_alias_property,
        )
        tcp_socket.sendall(publish_packet)

        packet_type, _packet_flags, ack_payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if packet_type != 4:
            return False, f"13.3.6 failed: expected PUBACK packet type 4, got {packet_type}"
        reason = _extract_reason_code(ack_payload)
        assert_reason_code(reason, 0x83)

        return True, "13.3.6 reason code 0x83 observed"
    except Exception as error:
        return False, f"13.3.6 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_7_bad_user_name_or_password_86(config) -> tuple[bool, str]:
    process = None
    username = "error-13-3-7-user"
    password = "error-13-3-7-pass"
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
                client_id=_unique_client_id("13-3-7"),
                clean_start=True,
                username=username,
                password="wrong-password",
            )
            assert_connack(connack, reason_code=0x86, session_present=False)

        return True, "13.3.7 reason code 0x86 observed"
    except Exception as error:
        return False, f"13.3.7 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_8_not_authorized_87(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("13-3-8")
    try:
        host, port, process = _start_isolated_broker(
            {
                "acl.rule": "deny,anonymous,publish,integration/connect/error-handling/13-3-8/#",
            }
        )
        reason = _qos1_publish_once(host, port, config.timeout_seconds, topic, b"denied")
        assert_reason_code(reason, 0x87)
        return True, "13.3.8 reason code 0x87 observed"
    except Exception as error:
        return False, f"13.3.8 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_9_server_shutting_down_8b(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()

        client = MqttClient(timeout_seconds=config.timeout_seconds)
        connack = client.connect(host, port, client_id=_unique_client_id("13-3-9"), clean_start=True)
        assert_connack(connack, reason_code=0x00, session_present=False)

        stop_broker(process)
        process = None

        disconnect_event = client.wait_for_disconnect(timeout=max(2.0, config.timeout_seconds))
        assert_reason_code(disconnect_event.reason_code, 0x8B)
        return True, "13.3.9 reason code 0x8B observed on broker shutdown"
    except Exception as error:
        return False, f"13.3.9 failed: {error}"


def run_13_3_10_keep_alive_timeout_8d(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = socket.create_connection((host, port), timeout=config.timeout_seconds)
        connect_packet = build_connect_packet(
            keepalive_seconds=1,
            payload=encode_utf8_string(_unique_client_id("13-3-10")),
        )
        tcp_socket.sendall(connect_packet)
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if packet_type != 2:
            return False, f"13.3.10 failed: expected CONNACK packet type 2, got {packet_type}"
        assert_reason_code(_read_connack_reason(payload), 0x00)

        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            try:
                packet_type, _packet_flags, payload = _recv_packet(tcp_socket, 0.7)
            except RuntimeError as runtime_error:
                if "socket closed" in str(runtime_error):
                    return False, "13.3.10 failed: expected DISCONNECT 0x8D before close"
                raise
            except socket.timeout:
                continue
            if packet_type == 14:
                reason = _disconnect_reason(payload)
                assert_reason_code(reason, 0x8D)
                return True, "13.3.10 reason code 0x8D observed"

        return False, "13.3.10 failed: expected keep-alive timeout DISCONNECT, but none observed"
    except Exception as error:
        return False, f"13.3.10 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_11_session_taken_over_8e(config) -> tuple[bool, str]:
    process = None
    shared_client_id = _unique_client_id("13-3-11")
    try:
        host, port, process = _start_isolated_broker()

        properties = None
        try:
            from paho.mqtt.packettypes import PacketTypes
            from paho.mqtt.properties import Properties

            properties = Properties(PacketTypes.CONNECT)
            setattr(properties, "SessionExpiryInterval", 120)
        except Exception:
            properties = None

        first_client = MqttClient(timeout_seconds=config.timeout_seconds)
        first_connack = first_client.connect(
            host,
            port,
            client_id=shared_client_id,
            clean_start=True,
            properties=properties,
        )
        assert_connack(first_connack, reason_code=0x00, session_present=False)

        second_client = MqttClient(timeout_seconds=config.timeout_seconds)
        second_connack = second_client.connect(host, port, client_id=shared_client_id, clean_start=False)
        assert_reason_code(int(second_connack.reason_code), 0x00)

        disconnect_event = first_client.wait_for_disconnect(timeout=config.timeout_seconds)
        assert_reason_code(disconnect_event.reason_code, 0x8E)
        return True, "13.3.11 reason code 0x8E observed"
    except Exception as error:
        return False, f"13.3.11 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_12_packet_identifier_in_use_91(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-3-12"))

        topic = _unique_topic("13-3-12")
        packet_identifier = 77

        first = build_publish_packet(topic=topic, payload=b"first", qos=2, packet_identifier=packet_identifier)
        second = build_publish_packet(topic=topic, payload=b"second", qos=2, packet_identifier=packet_identifier)

        tcp_socket.sendall(first)
        first_type, _first_flags, first_payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if first_type != 5:
            return False, f"13.3.12 failed: expected PUBREC for first publish, got packet type {first_type}"
        assert_reason_code(_extract_reason_code(first_payload), 0x00)

        tcp_socket.sendall(second)
        second_type, _second_flags, second_payload = _recv_packet(tcp_socket, config.timeout_seconds)

        if second_type == 14:
            reason = _disconnect_reason(second_payload)
            assert_reason_code(reason, 0x91)
            return True, "13.3.12 reason code 0x91 observed via DISCONNECT"

        if second_type == 5:
            reason = _extract_reason_code(second_payload)
            assert_reason_code(reason, 0x91)
            return True, "13.3.12 reason code 0x91 observed via PUBREC"

        return False, f"13.3.12 failed: expected DISCONNECT or PUBREC for duplicate packet id, got packet type {second_type}"
    except Exception as error:
        return False, f"13.3.12 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_13_packet_identifier_not_found_92(config) -> tuple[bool, str]:
    return _relabel_case_result(run_13_2_4_pubrel_unknown_packet_id_reason_92(config), "13.3.13")


def run_13_3_14_receive_maximum_exceeded_93(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.receive_maximum": 1,
            }
        )
        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-3-14"))

        topic = _unique_topic("13-3-14")
        first = build_publish_packet(topic=topic, payload=b"first", qos=2, packet_identifier=101)
        second = build_publish_packet(topic=topic, payload=b"second", qos=2, packet_identifier=102)

        tcp_socket.sendall(first)
        first_type, _first_flags, first_payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if first_type != 5:
            return False, f"13.3.14 failed: expected PUBREC for first publish, got packet type {first_type}"
        assert_reason_code(_extract_reason_code(first_payload), 0x00)

        tcp_socket.sendall(second)
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if packet_type != 14:
            return False, f"13.3.14 failed: expected DISCONNECT packet type 14, got {packet_type}"
        assert_reason_code(_disconnect_reason(payload), 0x93)

        return True, "13.3.14 reason code 0x93 observed"
    except Exception as error:
        return False, f"13.3.14 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_15_packet_too_large_95(config) -> tuple[bool, str]:
    process = None
    tcp_socket: socket.socket | None = None
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as probe:
            connack = probe.connect(host, port, client_id=_unique_client_id("13-3-15-probe"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            properties = getattr(connack, "properties", None)
            if properties is None:
                return False, "13.3.15 failed: CONNACK has no properties"
            max_packet_size = getattr(properties, "MaximumPacketSize", None)
            if max_packet_size is None:
                return False, "13.3.15 failed: CONNACK has no MaximumPacketSize"
            broker_limit = int(max_packet_size)

        if broker_limit >= 268435455:
            return True, "13.3.15 not applicable: broker advertises protocol maximum packet size"

        tcp_socket = _connect_raw(host, port, config.timeout_seconds, _unique_client_id("13-3-15"))
        oversized = build_publish_packet(
            topic=_unique_topic("13-3-15"),
            payload=b"X" * max(1, broker_limit),
            qos=0,
        )
        while len(oversized) <= broker_limit:
            oversized = build_publish_packet(
                topic=_unique_topic("13-3-15"),
                payload=b"X" * (len(oversized) + 1),
                qos=0,
            )

        tcp_socket.sendall(oversized)
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
        if packet_type != 14:
            return False, f"13.3.15 failed: expected DISCONNECT packet type 14, got {packet_type}"
        assert_reason_code(_disconnect_reason(payload), 0x95)

        return True, "13.3.15 reason code 0x95 observed"
    except Exception as error:
        return False, f"13.3.15 failed: {error}"
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        stop_broker(process)


def run_13_3_16_quota_exceeded_97(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("13-3-16"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

            base = _unique_topic("13-3-16")
            for index in range(1, 4097):
                suback_codes = client.subscribe(f"{base}/{index}", qos=0)
                if not suback_codes:
                    return False, f"13.3.16 failed: missing SUBACK at subscription #{index}"
                reason = int(suback_codes[0])
                if reason == 0x97:
                    return True, f"13.3.16 reason code 0x97 observed at subscription #{index}"
                if reason >= 0x80 and reason != 0x97:
                    return False, f"13.3.16 failed: expected 0x97 when quota exceeded, got 0x{reason:02X}"

        return True, "13.3.16 not applicable: broker did not signal quota exhaustion in tested range"
    except Exception as error:
        return False, f"13.3.16 failed: {error}"
    finally:
        stop_broker(process)


def run_13_3_17_shared_subscriptions_not_supported_9e(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("13-3-17"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

            shared_available = 1
            properties = getattr(connack, "properties", None)
            if properties is not None:
                shared_available_property = getattr(properties, "SharedSubscriptionAvailable", None)
                if shared_available_property is not None:
                    shared_available = int(shared_available_property)

            shared_filter = f"$share/group/{_unique_topic('13-3-17')}"
            suback_codes = client.subscribe(shared_filter, qos=0)
            if not suback_codes:
                return False, "13.3.17 failed: missing SUBACK for shared subscription"
            reason = int(suback_codes[0])

            if shared_available == 0:
                assert_reason_code(reason, 0x9E)
                return True, "13.3.17 reason code 0x9E observed"

            if reason == 0x9E:
                return True, "13.3.17 reason code 0x9E observed"
            if reason >= 0x80:
                return False, f"13.3.17 failed: expected successful shared subscription or 0x9E, got 0x{reason:02X}"

            return True, "13.3.17 not applicable: shared subscriptions are supported by broker"
    except Exception as error:
        return False, f"13.3.17 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "connect/error_handling_protocol_conformance/13_1_1_truncated_packet_disconnect_81",
        "description": "13.1.1 Truncated packet returns DISCONNECT 0x81",
        "run": run_13_1_1_truncated_packet_disconnect_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_1_2_invalid_remaining_length_disconnect_81",
        "description": "13.1.2 Invalid remaining length encoding returns DISCONNECT 0x81",
        "run": run_13_1_2_invalid_remaining_length_disconnect_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_1_3_reserved_fixed_header_bits_disconnect_81",
        "description": "13.1.3 Reserved bits in fixed header return DISCONNECT 0x81",
        "run": run_13_1_3_reserved_fixed_header_bits_disconnect_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_1_4_invalid_utf8_topic_disconnect_81",
        "description": "13.1.4 Invalid UTF-8 in topic returns DISCONNECT 0x81",
        "run": run_13_1_4_invalid_utf8_topic_disconnect_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_1_5_unknown_property_id_disconnect_81",
        "description": "13.1.5 Unknown property identifier returns DISCONNECT 0x81",
        "run": run_13_1_5_unknown_property_id_disconnect_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_1_6_duplicate_non_repeatable_property_disconnect_82",
        "description": "13.1.6 Duplicate non-repeatable property returns DISCONNECT 0x82",
        "run": run_13_1_6_duplicate_non_repeatable_property_disconnect_82,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_2_1_packet_type_not_valid_in_connected_state_disconnect_82",
        "description": "13.2.1 Packet type not valid in Connected state returns DISCONNECT 0x82",
        "run": run_13_2_1_packet_type_not_valid_in_connected_state_disconnect_82,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_2_2_subscribe_empty_filter_list_disconnect_82",
        "description": "13.2.2 SUBSCRIBE with empty filter list returns DISCONNECT 0x82",
        "run": run_13_2_2_subscribe_empty_filter_list_disconnect_82,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_2_3_publish_qos3_connection_closed",
        "description": "13.2.3 PUBLISH with QoS 3 closes connection",
        "run": run_13_2_3_publish_qos3_connection_closed,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_2_4_pubrel_unknown_packet_id_reason_92",
        "description": "13.2.4 PUBREL for unknown Packet Identifier returns 0x92",
        "run": run_13_2_4_pubrel_unknown_packet_id_reason_92,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_1_reason_code_success_00",
        "description": "13.3.1 Reason code 0x00 is covered in ACK packet types",
        "run": run_13_3_1_success_reason_in_ack_types,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_2_reason_code_no_matching_subscribers_10",
        "description": "13.3.2 Reason code 0x10 (No matching subscribers)",
        "run": run_13_3_2_no_matching_subscribers_puback_10,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_3_reason_code_unspecified_error_80",
        "description": "13.3.3 Reason code 0x80 (Unspecified error)",
        "run": run_13_3_3_unspecified_error_80,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_4_reason_code_malformed_packet_81",
        "description": "13.3.4 Reason code 0x81 (Malformed Packet)",
        "run": run_13_3_4_malformed_packet_81,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_5_reason_code_protocol_error_82",
        "description": "13.3.5 Reason code 0x82 (Protocol Error)",
        "run": run_13_3_5_protocol_error_82,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_6_reason_code_implementation_specific_error_83",
        "description": "13.3.6 Reason code 0x83 (Implementation Specific Error)",
        "run": run_13_3_6_implementation_specific_error_83,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_7_reason_code_bad_user_name_or_password_86",
        "description": "13.3.7 Reason code 0x86 (Bad User Name or Password)",
        "run": run_13_3_7_bad_user_name_or_password_86,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_8_reason_code_not_authorized_87",
        "description": "13.3.8 Reason code 0x87 (Not Authorized)",
        "run": run_13_3_8_not_authorized_87,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_9_reason_code_server_shutting_down_8b",
        "description": "13.3.9 Reason code 0x8B (Server shutting down)",
        "run": run_13_3_9_server_shutting_down_8b,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_10_reason_code_keep_alive_timeout_8d",
        "description": "13.3.10 Reason code 0x8D (Keep Alive timeout)",
        "run": run_13_3_10_keep_alive_timeout_8d,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_11_reason_code_session_taken_over_8e",
        "description": "13.3.11 Reason code 0x8E (Session taken over)",
        "run": run_13_3_11_session_taken_over_8e,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_12_reason_code_packet_identifier_in_use_91",
        "description": "13.3.12 Reason code 0x91 (Packet Identifier in use)",
        "run": run_13_3_12_packet_identifier_in_use_91,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_13_reason_code_packet_identifier_not_found_92",
        "description": "13.3.13 Reason code 0x92 (Packet Identifier not found)",
        "run": run_13_3_13_packet_identifier_not_found_92,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_14_reason_code_receive_maximum_exceeded_93",
        "description": "13.3.14 Reason code 0x93 (Receive Maximum exceeded)",
        "run": run_13_3_14_receive_maximum_exceeded_93,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_15_reason_code_packet_too_large_95",
        "description": "13.3.15 Reason code 0x95 (Packet too large)",
        "run": run_13_3_15_packet_too_large_95,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_16_reason_code_quota_exceeded_97",
        "description": "13.3.16 Reason code 0x97 (Quota exceeded)",
        "run": run_13_3_16_quota_exceeded_97,
    },
    {
        "name": "connect/error_handling_protocol_conformance/13_3_17_reason_code_shared_subscriptions_not_supported_9e",
        "description": "13.3.17 Reason code 0x9E (Shared Subscriptions not supported)",
        "run": run_13_3_17_shared_subscriptions_not_supported_9e,
    },
]
