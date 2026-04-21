"""Integration tests for interoperability section 20.2 (Edge Cases from Spec)."""

from __future__ import annotations

import importlib.util
import socket
import time
import uuid
from pathlib import Path


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
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
encode_variable_byte_integer = _raw_tcp_module.encode_variable_byte_integer


def _unique_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker(overrides: dict | None = None):
    effective: dict = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    if overrides:
        effective.update(overrides)
    process = start_broker(effective)
    host = _broker_module.resolve_target_host("127.0.0.1")
    return host, int(effective["network.mqtt_port"]), process


# ---------------------------------------------------------------------------
# Low-level raw TCP helpers (shared by multiple tests in this file)
# ---------------------------------------------------------------------------

def _read_exact_bytes(tcp_socket: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = tcp_socket.recv(n - len(buf))
        if chunk == b"":
            raise RuntimeError("connection closed while reading")
        buf.extend(chunk)
    return bytes(buf)


def _decode_variable_byte_integer(data: bytes, offset: int) -> tuple[int, int]:
    multiplier = 1
    value = 0
    consumed = 0
    while True:
        if offset + consumed >= len(data):
            raise ValueError("malformed variable byte integer")
        byte = data[offset + consumed]
        consumed += 1
        value += (byte & 0x7F) * multiplier
        if (byte & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise ValueError("variable byte integer exceeds MQTT limit")
    return value, consumed


def _read_mqtt_packet(tcp_socket: socket.socket, timeout: float) -> bytes:
    tcp_socket.settimeout(timeout)
    first = _read_exact_bytes(tcp_socket, 1)
    remaining_bytes = bytearray()
    while True:
        b = _read_exact_bytes(tcp_socket, 1)
        remaining_bytes.extend(b)
        if (b[0] & 0x80) == 0:
            break
        if len(remaining_bytes) >= 4:
            raise RuntimeError("invalid MQTT remaining length")
    remaining_length, _ = _decode_variable_byte_integer(bytes(remaining_bytes), 0)
    payload = _read_exact_bytes(tcp_socket, remaining_length)
    return first + bytes(remaining_bytes) + payload


def _packet_type(packet: bytes) -> int:
    return packet[0] >> 4


def _packet_payload(packet: bytes) -> bytes:
    remaining_length, consumed = _decode_variable_byte_integer(packet, 1)
    start = 1 + consumed
    return packet[start:start + remaining_length]


def _connack_reason_code(packet: bytes) -> int:
    if _packet_type(packet) != 2:
        raise AssertionError(f"expected CONNACK (type 2), got type {_packet_type(packet)}")
    payload = _packet_payload(packet)
    if len(payload) < 2:
        raise AssertionError("CONNACK payload too short")
    return int(payload[1])


def _open_raw_connected_socket(host: str, port: int, timeout: float) -> socket.socket:
    """Connect via raw TCP and complete CONNECT/CONNACK handshake."""
    tcp_socket = socket.create_connection((host, port), timeout=timeout)
    tcp_socket.settimeout(timeout)
    connect_packet = build_connect_packet(
        protocol_name="MQTT",
        protocol_version=5,
        connect_flags=0x02,  # CleanStart=1
        keepalive_seconds=30,
        properties=b"",
        payload=encode_utf8_string(_unique_id("raw-client")),
    )
    tcp_socket.sendall(connect_packet)
    connack = _read_mqtt_packet(tcp_socket, timeout)
    assert_reason_code(_connack_reason_code(connack), 0x00)
    return tcp_socket


def _socket_is_closed(tcp_socket: socket.socket, timeout: float) -> bool:
    tcp_socket.settimeout(0.2)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            data = tcp_socket.recv(1)
        except socket.timeout:
            continue
        if data == b"":
            return True
    return False


# ---------------------------------------------------------------------------
# Test 20.2.1 — Empty topic filter in SUBSCRIBE → Protocol Error
# ---------------------------------------------------------------------------

def run_20_2_1_empty_topic_filter_subscribe_rejected(config) -> tuple[bool, str]:
    """20.2.1 — Empty topic filter in SUBSCRIBE is a Protocol Error (MQTT 5.0 §4.7.3).

    The broker MUST reject an empty topic filter with a SUBACK reason code 0x82
    (Protocol Error) or close the connection with DISCONNECT 0x82.
    """
    process = None
    tcp_socket = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        tcp_socket = _open_raw_connected_socket(host, port, timeout)

        subscribe_packet = build_subscribe_packet(
            topic_filters=[("", 0)],
            packet_identifier=1,
        )
        tcp_socket.sendall(subscribe_packet)

        response = _read_mqtt_packet(tcp_socket, timeout)
        ptype = _packet_type(response)

        if ptype == 9:
            # SUBACK — reason code is in the payload after the 2-byte packet identifier
            payload = _packet_payload(response)
            if len(payload) < 3:
                return False, f"SUBACK payload too short: {payload!r}"
            reason_code = payload[2]
            if reason_code == 0x82:
                return True, "broker returned SUBACK 0x82 (Protocol Error) for empty topic filter"
            return False, f"SUBACK reason code 0x{reason_code:02X} — expected 0x82"

        if ptype == 14:
            # DISCONNECT — reason code 0x82
            dpayload = _packet_payload(response)
            dreason = int(dpayload[0]) if dpayload else 0x00
            if dreason == 0x82:
                return True, "broker sent DISCONNECT 0x82 (Protocol Error) for empty topic filter"
            return False, f"DISCONNECT reason code 0x{dreason:02X} — expected 0x82"

        return False, f"unexpected packet type {ptype} after SUBSCRIBE with empty filter"

    except Exception as exc:
        return False, str(exc)
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.2 — UTF-8 multi-byte topic names delivered correctly
# ---------------------------------------------------------------------------

def run_20_2_2_utf8_multibyte_topic_delivered(config) -> tuple[bool, str]:
    """20.2.2 — UTF-8 topic names with multi-byte characters are delivered correctly.

    Topics containing non-ASCII Unicode characters (e.g. Japanese, emoji) MUST be
    accepted and routed to matching subscribers without corruption.
    """
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        # Multi-byte topic using Japanese characters and emoji (4-byte UTF-8 codepoints)
        unique_suffix = uuid.uuid4().hex[:8]
        topic = f"integration/interop/テスト/🚀/{unique_suffix}"
        payload = "こんにちは世界 🌍".encode("utf-8")

        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("utf8-sub"), clean_start=True)
            sub.subscribe(topic, qos=0)

            with MqttClient(timeout_seconds=timeout) as pub:
                pub.connect(host, port, client_id=_unique_id("utf8-pub"), clean_start=True)
                pub.publish(topic, payload, qos=0)

            msgs = sub.collect_messages(count=1, timeout=timeout)
            assert_message(msgs[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, f"UTF-8 multi-byte topic delivered correctly: {topic!r}"

    except Exception as exc:
        return False, str(exc)
    finally:
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.3 — Null character (U+0000) in topic → rejected
# ---------------------------------------------------------------------------

def run_20_2_3_null_char_in_topic_rejected(config) -> tuple[bool, str]:
    """20.2.3 — Null character (U+0000) in topic name is rejected.

    Per MQTT 5.0 §1.5.4, a UTF-8 encoded string MUST NOT include the null
    character U+0000. Publishing to a topic containing U+0000 must be rejected;
    the broker MUST close the connection with DISCONNECT 0x81 (Malformed Packet).
    """
    process = None
    tcp_socket = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        tcp_socket = _open_raw_connected_socket(host, port, timeout)

        # Build PUBLISH packet with a topic containing U+0000 (\x00 embedded)
        topic_bytes = "test/null\x00char".encode("utf-8")
        topic_length_prefix = len(topic_bytes).to_bytes(2, "big")
        topic_field = topic_length_prefix + topic_bytes

        # PUBLISH fixed header: type 3, flags 0 (QoS 0, no retain, no dup)
        # Variable header: topic + 1-byte properties length
        properties_length = encode_variable_byte_integer(0)
        var_header = topic_field + properties_length
        remaining = len(var_header) + len(b"payload")
        publish_packet = (
            bytes([0x30])
            + encode_variable_byte_integer(remaining)
            + var_header
            + b"payload"
        )
        tcp_socket.sendall(publish_packet)

        response_bytes = bytearray()
        tcp_socket.settimeout(timeout)
        deadline = time.monotonic() + timeout
        closed = False
        while time.monotonic() < deadline:
            try:
                chunk = tcp_socket.recv(4096)
            except socket.timeout:
                continue
            if chunk == b"":
                closed = True
                break
            response_bytes.extend(chunk)
            # Check if we received a DISCONNECT or if connection got closed
            if response_bytes and _packet_type(bytes(response_bytes)) == 14:
                break

        if response_bytes and _packet_type(bytes(response_bytes)) == 14:
            payload = _packet_payload(bytes(response_bytes))
            dreason = int(payload[0]) if payload else 0x00
            if dreason in (0x81, 0x82, 0x99):
                return True, f"broker sent DISCONNECT 0x{dreason:02X} for null character in topic"
            return False, f"DISCONNECT reason code 0x{dreason:02X} — expected malformed/protocol error"

        if closed:
            return True, "broker closed connection on topic containing null character U+0000"

        return False, "broker did not reject topic containing null character U+0000"

    except Exception as exc:
        return False, str(exc)
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.4 — Zero-length Client ID with Clean Start = 0 → rejected
# ---------------------------------------------------------------------------

def run_20_2_4_zero_length_client_id_clean_start_false_rejected(config) -> tuple[bool, str]:
    """20.2.4 — Zero-length Client ID with Clean Start = 0 is rejected.

    MQTT 5.0 §3.1.3.1: if a Client supplies a zero-byte ClientID, it MUST set
    CleanStart to 1. The broker MUST send CONNACK 0x85 (Client Identifier not
    valid) and MUST close the connection if clean_start = 0 and ClientID is empty.
    """
    process = None
    tcp_socket = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        tcp_socket = socket.create_connection((host, port), timeout=timeout)
        tcp_socket.settimeout(timeout)

        # connect_flags: CleanStart=0 (bit 1 = 0), no other flags set → 0x00
        connect_packet = build_connect_packet(
            protocol_name="MQTT",
            protocol_version=5,
            connect_flags=0x00,  # CleanStart=0
            keepalive_seconds=30,
            properties=b"",
            payload=b"\x00\x00",  # empty UTF-8 string for ClientID
        )
        tcp_socket.sendall(connect_packet)

        connack = _read_mqtt_packet(tcp_socket, timeout)
        reason_code = _connack_reason_code(connack)

        if reason_code != 0x85:
            return False, (
                f"expected CONNACK 0x85 (Client Identifier not valid), "
                f"got 0x{reason_code:02X}"
            )

        closed = _socket_is_closed(tcp_socket, timeout=min(timeout, 3.0))
        if not closed:
            return False, "broker did not close connection after CONNACK 0x85"

        return True, "broker correctly rejected zero-length ClientID with CleanStart=0 (CONNACK 0x85)"

    except Exception as exc:
        return False, str(exc)
    finally:
        if tcp_socket is not None:
            try:
                tcp_socket.close()
            except OSError:
                pass
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.5 — Zero-length payload delivered correctly
# ---------------------------------------------------------------------------

def run_20_2_5_zero_length_payload_delivered(config) -> tuple[bool, str]:
    """20.2.5 — PUBLISH with zero-length payload is valid and delivered correctly.

    MQTT 5.0 §3.3.3: the payload is optional; a zero-length payload is a valid
    application message and MUST be delivered to subscribers.
    """
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        topic = f"integration/interop/20-2-5/{uuid.uuid4().hex}"

        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("empty-sub"), clean_start=True)
            sub.subscribe(topic, qos=1)

            with MqttClient(timeout_seconds=timeout) as pub:
                pub.connect(host, port, client_id=_unique_id("empty-pub"), clean_start=True)
                pub.publish(topic, b"", qos=1)

            msgs = sub.collect_messages(count=1, timeout=timeout)
            assert_message(msgs[0], topic=topic, payload=b"", qos=1, retain=False)

        return True, "zero-length payload delivered correctly"

    except Exception as exc:
        return False, str(exc)
    finally:
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.6 — PUBLISH to "/" delivered to subscribers of "/"
# ---------------------------------------------------------------------------

def run_20_2_6_publish_to_root_separator_topic(config) -> tuple[bool, str]:
    """20.2.6 — PUBLISH to "/" (single-separator topic) is valid and delivered.

    Per MQTT 5.0 §4.7.1, "/" is a valid topic name consisting of a single level
    separator. Publishing to "/" MUST be delivered to a subscriber of "/".
    """
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        topic = "/"
        payload = b"root-separator-payload"

        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("slash-sub"), clean_start=True)
            sub.subscribe(topic, qos=0)

            with MqttClient(timeout_seconds=timeout) as pub:
                pub.connect(host, port, client_id=_unique_id("slash-pub"), clean_start=True)
                pub.publish(topic, payload, qos=0)

            msgs = sub.collect_messages(count=1, timeout=timeout)
            assert_message(msgs[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, 'PUBLISH to "/" delivered correctly to subscriber of "/"'

    except Exception as exc:
        return False, str(exc)
    finally:
        if process is not None:
            stop_broker(process)


# ---------------------------------------------------------------------------
# Test 20.2.7 — Subscribe to "/" receives messages published to "/"
# ---------------------------------------------------------------------------

def run_20_2_7_subscribe_to_root_separator_receives_messages(config) -> tuple[bool, str]:
    """20.2.7 — Subscriber of "/" receives messages published to "/" only.

    A subscription to "/" matches the topic "/" and nothing else.
    Messages published to "//" or "a/b" MUST NOT be delivered to a subscriber of "/".
    """
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds

        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("slash-sub2"), clean_start=True)
            sub.subscribe("/", qos=0)

            with MqttClient(timeout_seconds=timeout) as pub:
                pub.connect(host, port, client_id=_unique_id("slash-pub2"), clean_start=True)
                pub.publish("/", b"match", qos=0)
                pub.publish("//", b"no-match", qos=0)
                pub.publish("a/b", b"no-match", qos=0)

            msgs = sub.collect_messages(count=1, timeout=timeout)
            if len(msgs) != 1:
                return False, f"expected 1 message for '/', got {len(msgs)}"
            assert_message(msgs[0], topic="/", payload=b"match", qos=0, retain=False)

            # Verify no extra messages arrive
            assert_no_message(sub, timeout=min(timeout / 4, 2.0))

        return True, 'subscriber of "/" received only messages for "/" — non-matching topics correctly excluded'

    except Exception as exc:
        return False, str(exc)
    finally:
        if process is not None:
            stop_broker(process)


TEST_CASES = [
    {
        "name": "interop/20_2_1_empty_topic_filter_subscribe_rejected",
        "description": "20.2.1 Empty topic filter in SUBSCRIBE → Protocol Error",
        "run": run_20_2_1_empty_topic_filter_subscribe_rejected,
    },
    {
        "name": "interop/20_2_2_utf8_multibyte_topic_delivered",
        "description": "20.2.2 UTF-8 topic names with multi-byte characters → delivered correctly",
        "run": run_20_2_2_utf8_multibyte_topic_delivered,
    },
    {
        "name": "interop/20_2_3_null_char_in_topic_rejected",
        "description": "20.2.3 Null character (U+0000) in topic → rejected",
        "run": run_20_2_3_null_char_in_topic_rejected,
    },
    {
        "name": "interop/20_2_4_zero_length_client_id_clean_start_false_rejected",
        "description": "20.2.4 Zero-length Client ID with Clean Start = 0 → rejected",
        "run": run_20_2_4_zero_length_client_id_clean_start_false_rejected,
    },
    {
        "name": "interop/20_2_5_zero_length_payload_delivered",
        "description": "20.2.5 Payload with zero length → delivered correctly",
        "run": run_20_2_5_zero_length_payload_delivered,
    },
    {
        "name": "interop/20_2_6_publish_to_root_separator_topic",
        "description": '20.2.6 PUBLISH to "/" (single-separator topic) → delivered to subscribers of "/"',
        "run": run_20_2_6_publish_to_root_separator_topic,
    },
    {
        "name": "interop/20_2_7_subscribe_to_root_separator_receives_messages",
        "description": '20.2.7 Subscribe to "/" → receives messages published to "/"',
        "run": run_20_2_7_subscribe_to_root_separator_receives_messages,
    },
]
