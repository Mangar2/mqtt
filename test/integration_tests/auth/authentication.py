"""Integration tests for authentication section 10.1 to 10.3."""

from __future__ import annotations

import importlib.util
import os
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
encode_variable_byte_integer = _raw_tcp_module.encode_variable_byte_integer


_AUTH_METHOD_PROPERTY_ID = 0x15
_AUTH_DATA_PROPERTY_ID = 0x16
_USER_PROPERTY_ID = 0x26
_REASON_STRING_PROPERTY_ID = 0x1F

_AUTH_USERNAME = "default-user"
_AUTH_PASSWORD = "default-pass"
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"


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


def _require_managed_broker_in_remote(required_overrides: str) -> None:
    if os.environ.get(_BROKER_MANAGED_ENV, "").strip() != "0":
        return
    raise _broker_module.ManagedBrokerRequired(
        f"requires managed broker startup (requested overrides: {required_overrides})"
    )


def _encode_auth_properties(auth_method: str | None = None, auth_data: bytes | None = None) -> bytes:
    properties = bytearray()
    if auth_method is not None:
        properties.append(_AUTH_METHOD_PROPERTY_ID)
        properties.extend(encode_utf8_string(auth_method))
    if auth_data is not None:
        properties.append(_AUTH_DATA_PROPERTY_ID)
        properties.extend(len(auth_data).to_bytes(2, byteorder="big"))
        properties.extend(auth_data)
    return bytes(properties)


def _build_connect_payload(client_id: str, username: str | None = None, password: str | None = None) -> bytes:
    payload = bytearray()
    payload.extend(encode_utf8_string(client_id))

    if username is not None:
        payload.extend(encode_utf8_string(username))
    if password is not None:
        payload.extend(len(password.encode("utf-8")).to_bytes(2, byteorder="big"))
        payload.extend(password.encode("utf-8"))

    return bytes(payload)


def _build_connect_packet_with_auth(
    client_id: str,
    auth_method: str,
    auth_data: bytes | None = None,
    username: str | None = None,
    password: str | None = None,
) -> bytes:
    connect_flags = 0x02
    if password is not None:
        connect_flags |= 0x40
    if username is not None:
        connect_flags |= 0x80

    properties = _encode_auth_properties(auth_method=auth_method, auth_data=auth_data)
    payload = _build_connect_payload(client_id=client_id, username=username, password=password)

    return build_connect_packet(
        connect_flags=connect_flags,
        keepalive_seconds=30,
        properties=properties,
        payload=payload,
    )


def _build_auth_packet(reason_code: int, auth_method: str, auth_data: bytes | None = None) -> bytes:
    properties = _encode_auth_properties(auth_method=auth_method, auth_data=auth_data)
    variable_header = bytes([reason_code]) + encode_variable_byte_integer(len(properties)) + properties
    return build_packet(packet_type=15, flags=0, variable_header=variable_header, payload=b"")


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

    remaining_length = 0
    multiplier = 1
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


def _decode_variable_byte_integer(buffer_bytes: bytes, offset: int = 0) -> tuple[int, int]:
    value = 0
    multiplier = 1
    index = offset

    while True:
        if index >= len(buffer_bytes):
            raise RuntimeError("incomplete variable byte integer")
        encoded = buffer_bytes[index]
        value += (encoded & 0x7F) * multiplier
        index += 1

        if (encoded & 0x80) == 0:
            return value, index - offset

        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise RuntimeError("malformed variable byte integer")


def _parse_properties(buffer_bytes: bytes) -> dict[int, object]:
    properties: dict[int, object] = {}
    index = 0

    def _read_utf8(start_index: int) -> tuple[str, int]:
        if start_index + 2 > len(buffer_bytes):
            raise RuntimeError("truncated UTF-8 property length")
        string_length = int.from_bytes(buffer_bytes[start_index:start_index + 2], byteorder="big")
        value_start = start_index + 2
        value_end = value_start + string_length
        if value_end > len(buffer_bytes):
            raise RuntimeError("truncated UTF-8 property value")
        return buffer_bytes[value_start:value_end].decode("utf-8"), value_end

    def _read_binary(start_index: int) -> tuple[bytes, int]:
        if start_index + 2 > len(buffer_bytes):
            raise RuntimeError("truncated binary property length")
        binary_length = int.from_bytes(buffer_bytes[start_index:start_index + 2], byteorder="big")
        value_start = start_index + 2
        value_end = value_start + binary_length
        if value_end > len(buffer_bytes):
            raise RuntimeError("truncated binary property value")
        return bytes(buffer_bytes[value_start:value_end]), value_end

    def _skip_fixed_length(start_index: int, length: int) -> int:
        value_end = start_index + length
        if value_end > len(buffer_bytes):
            raise RuntimeError("truncated fixed-length property value")
        return value_end

    while index < len(buffer_bytes):
        property_id = int(buffer_bytes[index])
        index += 1

        if property_id == _AUTH_METHOD_PROPERTY_ID:
            properties[property_id], index = _read_utf8(index)
            continue

        if property_id == _AUTH_DATA_PROPERTY_ID:
            properties[property_id], index = _read_binary(index)
            continue

        if property_id in (_REASON_STRING_PROPERTY_ID, 0x12, 0x13, 0x1A, 0x1C):
            _, index = _read_utf8(index)
            continue

        if property_id == _USER_PROPERTY_ID:
            _, index = _read_utf8(index)
            _, index = _read_utf8(index)
            continue

        if property_id in (0x11, 0x27):
            index = _skip_fixed_length(index, 4)
            continue

        if property_id in (0x21, 0x22, 0x13):
            index = _skip_fixed_length(index, 2)
            continue

        if property_id in (0x18, 0x19, 0x24, 0x25, 0x28, 0x29, 0x2A):
            index = _skip_fixed_length(index, 1)
            continue

        raise RuntimeError(f"unsupported property in auth parser: 0x{property_id:02X}")

    return properties


def _parse_connack(payload: bytes) -> tuple[int, dict[int, object]]:
    if len(payload) < 2:
        raise RuntimeError("invalid CONNACK payload")

    reason_code = int(payload[1])
    property_length, consumed = _decode_variable_byte_integer(payload, offset=2)
    properties_start = 2 + consumed
    properties_end = properties_start + property_length
    if properties_end > len(payload):
        raise RuntimeError("truncated CONNACK properties")
    properties = _parse_properties(payload[properties_start:properties_end])
    return reason_code, properties


def _parse_auth(payload: bytes) -> tuple[int, dict[int, object]]:
    if not payload:
        return 0x00, {}

    reason_code = int(payload[0])
    if len(payload) == 1:
        return reason_code, {}

    property_length, consumed = _decode_variable_byte_integer(payload, offset=1)
    properties_start = 1 + consumed
    properties_end = properties_start + property_length
    if properties_end > len(payload):
        raise RuntimeError("truncated AUTH properties")
    properties = _parse_properties(payload[properties_start:properties_end])
    return reason_code, properties


def _parse_disconnect_reason(payload: bytes) -> int:
    if not payload:
        return 0x00
    return int(payload[0])


def _open_raw_connection(host: str, port: int, timeout_seconds: float) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    tcp_socket.settimeout(timeout_seconds)
    return tcp_socket


def run_10_1_1_valid_credentials_connack_success(config) -> tuple[bool, str]:
    process = None
    username = _AUTH_USERNAME
    password = _AUTH_PASSWORD

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
                client_id=_unique_client_id("10-1-1"),
                clean_start=True,
                username=username,
                password=password,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

        return True, "10.1.1 valid credentials accepted"
    except Exception as error:
        return False, f"10.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_10_1_2_invalid_credentials_connack_0x86(config) -> tuple[bool, str]:
    process = None
    username = _AUTH_USERNAME
    password = _AUTH_PASSWORD

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
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
                client_id=_unique_client_id("10-1-2"),
                clean_start=True,
                username=username,
                password="wrong-pass",
            )
            assert_connack(connack, reason_code=0x86, session_present=False)

        return True, "10.1.2 invalid credentials rejected with 0x86"
    except Exception as error:
        return False, f"10.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_10_1_3_missing_required_credentials_connack_0x86(config) -> tuple[bool, str]:
    process = None
    username = _AUTH_USERNAME
    password = _AUTH_PASSWORD

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
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
                client_id=_unique_client_id("10-1-3"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x86, session_present=False)

        return True, "10.1.3 missing required credentials rejected with 0x86"
    except Exception as error:
        return False, f"10.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_10_2_1_connect_with_auth_method_uses_auth_packets(config) -> tuple[bool, str]:
    process = None
    auth_method = "PLAIN"

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = _open_raw_connection(host, port, config.timeout_seconds)
        try:
            connect_packet = _build_connect_packet_with_auth(
                client_id=_unique_client_id("10-2-1"),
                auth_method=auth_method,
                auth_data=None,
            )
            tcp_socket.sendall(connect_packet)

            packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
            if packet_type != 15:
                return False, f"expected AUTH packet (type 15), got packet type {packet_type}"

            reason_code, properties = _parse_auth(payload)
            if reason_code != 0x18:
                return False, f"expected AUTH reason 0x18, got 0x{reason_code:02X}"

            received_method = properties.get(_AUTH_METHOD_PROPERTY_ID)
            if received_method != auth_method:
                return (
                    False,
                    "server AUTH Authentication Method mismatch: "
                    f"expected {auth_method!r}, got {received_method!r}",
                )

            client_auth = _build_auth_packet(
                reason_code=0x18,
                auth_method=auth_method,
                auth_data=f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}".encode("utf-8"),
            )
            tcp_socket.sendall(client_auth)

            follow_up_type, _follow_up_flags, _follow_up_payload = _recv_packet(
                tcp_socket,
                config.timeout_seconds,
            )
            if follow_up_type != 2:
                return False, f"expected CONNACK after AUTH response, got packet type {follow_up_type}"

            connack_reason, connack_properties = _parse_connack(_follow_up_payload)
            if connack_reason != 0x00:
                return False, f"expected CONNACK success after AUTH response, got 0x{connack_reason:02X}"
            if connack_properties.get(_AUTH_METHOD_PROPERTY_ID) != auth_method:
                return False, "successful CONNACK is missing matching Authentication Method"

            return True, "10.2.1 AUTH challenge-response flow observed and completed"
        finally:
            try:
                tcp_socket.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"10.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_10_2_2_successful_multistep_handshake_connack_success(config) -> tuple[bool, str]:
    process = None
    auth_method = "PLAIN"

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = _open_raw_connection(host, port, config.timeout_seconds)
        try:
            connect_packet = _build_connect_packet_with_auth(
                client_id=_unique_client_id("10-2-2"),
                auth_method=auth_method,
                auth_data=None,
            )
            tcp_socket.sendall(connect_packet)

            for step_index in range(1, 6):
                packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)

                if packet_type == 15:
                    reason_code, properties = _parse_auth(payload)
                    if reason_code != 0x18:
                        return False, f"expected AUTH reason 0x18, got 0x{reason_code:02X}"

                    server_method = properties.get(_AUTH_METHOD_PROPERTY_ID)
                    if server_method != auth_method:
                        return (
                            False,
                            "server AUTH Authentication Method mismatch: "
                            f"expected {auth_method!r}, got {server_method!r}",
                        )

                    client_auth = _build_auth_packet(
                        reason_code=0x18,
                        auth_method=auth_method,
                        auth_data=f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}".encode("utf-8"),
                    )
                    tcp_socket.sendall(client_auth)
                    continue

                if packet_type == 2:
                    reason_code, properties = _parse_connack(payload)
                    if reason_code != 0x00:
                        return False, f"expected CONNACK success 0x00, got 0x{reason_code:02X}"

                    connack_method = properties.get(_AUTH_METHOD_PROPERTY_ID)
                    if connack_method != auth_method:
                        return (
                            False,
                            "successful CONNACK missing matching Authentication Method: "
                            f"expected {auth_method!r}, got {connack_method!r}",
                        )

                    return True, "10.2.2 multi-step enhanced authentication ended with CONNACK success"

                if packet_type == 14:
                    disconnect_reason = _parse_disconnect_reason(payload)
                    return False, (
                        "expected successful handshake, but broker sent DISCONNECT "
                        f"0x{disconnect_reason:02X}"
                    )

                return False, f"unexpected packet type during handshake: {packet_type}"

            return False, "handshake did not reach CONNACK success within 5 AUTH exchange steps"
        finally:
            try:
                tcp_socket.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"10.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_10_2_3_failed_handshake_connack_0x86(config) -> tuple[bool, str]:
    process = None
    auth_method = "PLAIN"

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = _open_raw_connection(host, port, config.timeout_seconds)
        try:
            connect_packet = _build_connect_packet_with_auth(
                client_id=_unique_client_id("10-2-3"),
                auth_method=auth_method,
                auth_data=None,
            )
            tcp_socket.sendall(connect_packet)

            for step_index in range(1, 6):
                packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)

                if packet_type == 2:
                    reason_code, _properties = _parse_connack(payload)
                    if reason_code != 0x86:
                        return False, f"expected CONNACK 0x86, got 0x{reason_code:02X}"
                    return True, "10.2.3 failed enhanced handshake rejected with CONNACK 0x86"

                if packet_type == 15:
                    reason_code, _properties = _parse_auth(payload)
                    if reason_code != 0x18:
                        return False, f"expected AUTH reason 0x18 before rejection, got 0x{reason_code:02X}"

                    client_auth = _build_auth_packet(
                        reason_code=0x18,
                        auth_method=auth_method,
                        auth_data=b"invalid-credentials",
                    )
                    tcp_socket.sendall(client_auth)
                    continue

                if packet_type == 14:
                    disconnect_reason = _parse_disconnect_reason(payload)
                    return False, (
                        "expected CONNACK 0x86 for failed handshake, "
                        f"got DISCONNECT 0x{disconnect_reason:02X}"
                    )

                return False, f"unexpected packet type during failed-handshake test: {packet_type}"

            return False, "did not receive CONNACK 0x86 within 5 AUTH exchange steps"
        finally:
            try:
                tcp_socket.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"10.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_10_2_4_unknown_authentication_method_connack_0x8c(config) -> tuple[bool, str]:
    process = None

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = _open_raw_connection(host, port, config.timeout_seconds)
        try:
            connect_packet = _build_connect_packet_with_auth(
                client_id=_unique_client_id("10-2-4"),
                auth_method=f"unknown-{uuid.uuid4().hex}",
                auth_data=b"x",
            )
            tcp_socket.sendall(connect_packet)

            packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
            if packet_type != 2:
                return False, f"expected CONNACK packet (type 2), got packet type {packet_type}"

            reason_code, _properties = _parse_connack(payload)
            if reason_code != 0x8C:
                return False, f"expected CONNACK 0x8C, got 0x{reason_code:02X}"

            return True, "10.2.4 unknown authentication method rejected with CONNACK 0x8C"
        finally:
            try:
                tcp_socket.close()
            except OSError:
                pass
    except Exception as error:
        return False, f"10.2.4 failed: {error}"
    finally:
        stop_broker(process)


def _establish_successful_enhanced_auth_connection(
    host: str,
    port: int,
    timeout_seconds: float,
    auth_method: str,
) -> tuple[socket.socket, str]:
    tcp_socket = _open_raw_connection(host, port, timeout_seconds)
    client_id = _unique_client_id("10-2-established")

    connect_packet = _build_connect_packet_with_auth(
        client_id=client_id,
        auth_method=auth_method,
        auth_data=None,
    )
    tcp_socket.sendall(connect_packet)

    for step_index in range(1, 8):
        packet_type, _packet_flags, payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type == 15:
            reason_code, properties = _parse_auth(payload)
            if reason_code != 0x18:
                raise RuntimeError(f"expected AUTH reason 0x18, got 0x{reason_code:02X}")
            server_method = properties.get(_AUTH_METHOD_PROPERTY_ID)
            if server_method != auth_method:
                raise RuntimeError(
                    "server AUTH Authentication Method mismatch while establishing session: "
                    f"expected {auth_method!r}, got {server_method!r}"
                )
            tcp_socket.sendall(
                _build_auth_packet(
                    reason_code=0x18,
                    auth_method=auth_method,
                    auth_data=f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}".encode("utf-8"),
                )
            )
            continue

        if packet_type == 2:
            reason_code, _properties = _parse_connack(payload)
            if reason_code != 0x00:
                raise RuntimeError(f"expected CONNACK 0x00, got 0x{reason_code:02X}")
            return tcp_socket, client_id

        raise RuntimeError(f"unexpected packet type while establishing enhanced auth session: {packet_type}")

    raise RuntimeError("did not receive CONNACK success while establishing enhanced auth session")


def run_10_2_5_reauthentication_during_active_session_success(config) -> tuple[bool, str]:
    process = None
    auth_method = "PLAIN"

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = None
        try:
            tcp_socket, _client_id = _establish_successful_enhanced_auth_connection(
                host,
                port,
                config.timeout_seconds,
                auth_method,
            )

            reauth_packet = _build_auth_packet(
                reason_code=0x19,
                auth_method=auth_method,
                auth_data=None,
            )
            tcp_socket.sendall(reauth_packet)

            packet_type, _packet_flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
            if packet_type != 15:
                return False, f"expected AUTH response to re-authentication, got packet type {packet_type}"

            reason_code, properties = _parse_auth(payload)
            if reason_code not in (0x18, 0x00):
                return False, f"expected AUTH reason 0x18 or 0x00, got 0x{reason_code:02X}"

            server_method = properties.get(_AUTH_METHOD_PROPERTY_ID)
            if server_method != auth_method:
                return (
                    False,
                    "server re-auth response Authentication Method mismatch: "
                    f"expected {auth_method!r}, got {server_method!r}",
                )

            if reason_code == 0x18:
                tcp_socket.sendall(
                    _build_auth_packet(
                        reason_code=0x18,
                        auth_method=auth_method,
                        auth_data=f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}".encode("utf-8"),
                    )
                )
                second_packet_type, _second_flags, second_payload = _recv_packet(
                    tcp_socket,
                    config.timeout_seconds,
                )
                if second_packet_type != 15:
                    return False, f"expected final AUTH success after re-auth, got packet type {second_packet_type}"
                final_reason_code, final_properties = _parse_auth(second_payload)
                if final_reason_code != 0x00:
                    return False, f"expected final AUTH success 0x00, got 0x{final_reason_code:02X}"
                if final_properties.get(_AUTH_METHOD_PROPERTY_ID) != auth_method:
                    return False, "final re-auth AUTH packet has mismatched Authentication Method"

            return True, "10.2.5 re-authentication during active session succeeded"
        finally:
            if tcp_socket is not None:
                try:
                    tcp_socket.close()
                except OSError:
                    pass
    except Exception as error:
        return False, f"10.2.5 failed: {error}"
    finally:
        stop_broker(process)


def run_10_2_6_reauthentication_failure_disconnect(config) -> tuple[bool, str]:
    process = None
    auth_method = "PLAIN"

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        tcp_socket = None
        try:
            tcp_socket, _client_id = _establish_successful_enhanced_auth_connection(
                host,
                port,
                config.timeout_seconds,
                auth_method,
            )

            reauth_packet = _build_auth_packet(
                reason_code=0x19,
                auth_method=auth_method,
                auth_data=None,
            )
            tcp_socket.sendall(reauth_packet)

            first_packet_type, _first_flags, first_payload = _recv_packet(tcp_socket, config.timeout_seconds)
            if first_packet_type != 15:
                return False, f"expected AUTH challenge after re-auth request, got packet type {first_packet_type}"

            first_reason_code, _first_properties = _parse_auth(first_payload)
            if first_reason_code != 0x18:
                return False, f"expected AUTH continue 0x18 during failing re-auth, got 0x{first_reason_code:02X}"

            failing_response_packet = _build_auth_packet(
                reason_code=0x18,
                auth_method=auth_method,
                auth_data=b"invalid-reauth-data",
            )
            tcp_socket.sendall(failing_response_packet)

            second_packet_type, _second_flags, second_payload = _recv_packet(
                tcp_socket,
                config.timeout_seconds,
            )
            if second_packet_type != 14:
                return False, f"expected DISCONNECT packet (type 14), got packet type {second_packet_type}"

            disconnect_reason = _parse_disconnect_reason(second_payload)
            return True, f"10.2.6 re-authentication failure caused DISCONNECT 0x{disconnect_reason:02X}"
        finally:
            if tcp_socket is not None:
                try:
                    tcp_socket.close()
                except OSError:
                    pass
    except Exception as error:
        return False, f"10.2.6 failed: {error}"
    finally:
        stop_broker(process)


def run_10_3_1_anonymous_access_enabled_connect_succeeds(config) -> tuple[bool, str]:
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
                client_id=_unique_client_id("10-3-1"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

        return True, "10.3.1 anonymous connect succeeded when anonymous access is enabled"
    except Exception as error:
        return False, f"10.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_10_3_2_anonymous_access_disabled_connect_fails(config) -> tuple[bool, str]:
    process = None

    try:
        _require_managed_broker_in_remote("auth.credential, broker.allow_anonymous")
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": False,
                "auth.credential": f"{_AUTH_USERNAME}:{_AUTH_PASSWORD}",
            }
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=_unique_client_id("10-3-2"),
                clean_start=True,
            )
            if int(connack.reason_code) == 0x00:
                return False, "expected anonymous connect to fail when anonymous access is disabled"
            if int(connack.reason_code) not in (0x86, 0x87):
                return False, f"expected reject reason 0x86 or 0x87, got 0x{int(connack.reason_code):02X}"

        return True, "10.3.2 anonymous connect was rejected when anonymous access is disabled"
    except Exception as error:
        return False, f"10.3.2 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "auth/username_password/valid_credentials",
        "description": "10.1.1 Valid credentials return CONNACK success",
        "run": run_10_1_1_valid_credentials_connack_success,
    },
    {
        "name": "auth/username_password/invalid_credentials",
        "description": "10.1.2 Invalid credentials return CONNACK 0x86",
        "run": run_10_1_2_invalid_credentials_connack_0x86,
    },
    {
        "name": "auth/username_password/missing_required_credentials",
        "description": "10.1.3 Missing required credentials return CONNACK 0x86",
        "run": run_10_1_3_missing_required_credentials_connack_0x86,
    },
    {
        "name": "auth/enhanced/connect_with_auth_method_uses_auth_packets",
        "description": "10.2.1 CONNECT with Authentication Method performs AUTH challenge-response",
        "run": run_10_2_1_connect_with_auth_method_uses_auth_packets,
    },
    {
        "name": "auth/enhanced/successful_multistep_handshake",
        "description": "10.2.2 Successful multi-step handshake returns CONNACK success",
        "run": run_10_2_2_successful_multistep_handshake_connack_success,
    },
    {
        "name": "auth/enhanced/failed_handshake_connack_0x86",
        "description": "10.2.3 Failed handshake returns CONNACK 0x86",
        "run": run_10_2_3_failed_handshake_connack_0x86,
    },
    {
        "name": "auth/enhanced/unknown_authentication_method_connack_0x8c",
        "description": "10.2.4 Unknown Authentication Method returns CONNACK 0x8C",
        "run": run_10_2_4_unknown_authentication_method_connack_0x8c,
    },
    {
        "name": "auth/enhanced/reauthentication_active_session_success",
        "description": "10.2.5 Re-authentication during active session succeeds",
        "run": run_10_2_5_reauthentication_during_active_session_success,
    },
    {
        "name": "auth/enhanced/reauthentication_failure_disconnect",
        "description": "10.2.6 Re-authentication failure triggers DISCONNECT",
        "run": run_10_2_6_reauthentication_failure_disconnect,
    },
    {
        "name": "auth/anonymous_access/enabled_connect_succeeds",
        "description": "10.3.1 Anonymous access enabled allows connect without credentials",
        "run": run_10_3_1_anonymous_access_enabled_connect_succeeds,
    },
    {
        "name": "auth/anonymous_access/disabled_connect_fails",
        "description": "10.3.2 Anonymous access disabled rejects connect without credentials",
        "run": run_10_3_2_anonymous_access_disabled_connect_fails,
    },
]
