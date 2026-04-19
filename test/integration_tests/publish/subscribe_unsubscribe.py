"""Integration tests for SUBSCRIBE (2.5) and UNSUBSCRIBE (2.6)."""

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
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
build_connect_packet = _raw_tcp_module.build_connect_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
build_unsubscribe_packet = _raw_tcp_module.build_unsubscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/subscribe/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


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


def _recv_exact(sock: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("socket closed while reading packet")
        data.extend(chunk)
    return bytes(data)


def _recv_packet(sock: socket.socket, timeout_seconds: float) -> tuple[int, int, bytes]:
    sock.settimeout(timeout_seconds)
    first_byte = _recv_exact(sock, 1)[0]

    multiplier = 1
    remaining_length = 0
    while True:
        encoded = _recv_exact(sock, 1)[0]
        remaining_length += (encoded & 0x7F) * multiplier
        if (encoded & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise RuntimeError("malformed remaining length")

    payload = _recv_exact(sock, remaining_length)
    return (first_byte >> 4) & 0x0F, first_byte & 0x0F, payload


def _raw_connect(host: str, port: int, timeout_seconds: float, client_id: str) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
        sock.sendall(connect_packet)
        packet_type, _flags, payload = _recv_packet(sock, timeout_seconds)
        if packet_type != 2:
            raise RuntimeError(f"expected CONNACK (type 2), got type {packet_type}")
        if len(payload) < 2 or int(payload[1]) != 0x00:
            reason = int(payload[1]) if len(payload) >= 2 else 0xFF
            raise RuntimeError(f"CONNECT rejected: reason 0x{reason:02X}")
        return sock
    except Exception:
        sock.close()
        raise


def _parse_suback_or_unsuback_reason_codes(payload: bytes) -> list[int]:
    """Extract per-filter reason codes from a SUBACK or UNSUBACK payload."""
    if len(payload) < 3:
        raise RuntimeError(f"ACK payload too short: {len(payload)} bytes")
    props_len = int(payload[2])
    reason_code_start = 3 + props_len
    return list(payload[reason_code_start:])


# ─── 2.5 SUBSCRIBE ───────────────────────────────────────────────────────────

def run_2_5_1_subscribe_single_filter_suback_granted_qos(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-5-1")
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("sub-2-5-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.5.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)
        return True, "2.5.1 SUBACK returned granted QoS 1 for single topic filter"
    except Exception as error:
        return False, f"2.5.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_5_2_subscribe_multiple_filters_suback_per_filter(config) -> tuple[bool, str]:
    process = None
    topic_a = _unique_topic("2-5-2-a")
    topic_b = _unique_topic("2-5-2-b")
    try:
        host, port, process = _start_isolated_broker()
        sock = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-2-5-2"))
        try:
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(topic_a, 0), (topic_b, 1)],
                packet_identifier=5,
            )
            sock.sendall(subscribe_packet)
            packet_type, _flags, payload = _recv_packet(sock, config.timeout_seconds)
            if packet_type != 9:
                return False, f"2.5.2 expected SUBACK (type 9), got type {packet_type}"
            reason_codes = _parse_suback_or_unsuback_reason_codes(payload)
            if len(reason_codes) != 2:
                return False, f"2.5.2 expected 2 SUBACK reason codes, got {len(reason_codes)}"
            assert_reason_code(reason_codes[0], 0x00)
            assert_reason_code(reason_codes[1], 0x01)
        finally:
            sock.close()
        return True, "2.5.2 SUBACK included per-filter reason codes for multi-filter SUBSCRIBE"
    except Exception as error:
        return False, f"2.5.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_5_3_subscribe_invalid_topic_filter_suback_error(config) -> tuple[bool, str]:
    process = None
    try:
        host, port, process = _start_isolated_broker()
        sock = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-2-5-3"))
        reason_code_value = None
        try:
            # "#/invalid" is invalid: '#' must be the last character in a topic level
            subscribe_packet = build_subscribe_packet(
                topic_filters=[("#/invalid", 0)],
                packet_identifier=3,
            )
            sock.sendall(subscribe_packet)
            packet_type, _flags, payload = _recv_packet(sock, config.timeout_seconds)
            if packet_type != 9:
                return False, f"2.5.3 expected SUBACK (type 9), got type {packet_type}"
            reason_codes = _parse_suback_or_unsuback_reason_codes(payload)
            if not reason_codes:
                return False, "2.5.3 SUBACK has no reason codes"
            reason_code_value = int(reason_codes[0])
            if reason_code_value < 0x80:
                return False, f"2.5.3 expected error reason code (>=0x80), got 0x{reason_code_value:02X}"
        finally:
            sock.close()
        return True, f"2.5.3 SUBACK returned error reason code 0x{reason_code_value:02X} for invalid topic filter"
    except Exception as error:
        return False, f"2.5.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_5_4_subscribe_updates_existing_subscription(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-5-4")
    payload = b"qos1-after-resubscribe"
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-5-4"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.5.4 first SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.5.4 second SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-5-4"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publisher.publish(topic, payload, qos=1)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=1, retain=False)

        return True, "2.5.4 re-subscribe with higher QoS updated existing subscription"
    except Exception as error:
        return False, f"2.5.4 failed: {error}"
    finally:
        stop_broker(process)


def run_2_5_5_suback_packet_id_matches_subscribe(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-5-5")
    try:
        host, port, process = _start_isolated_broker()
        sock = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-2-5-5"))
        try:
            packet_identifier = 0x1234
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(topic, 0)],
                packet_identifier=packet_identifier,
            )
            sock.sendall(subscribe_packet)
            packet_type, _flags, payload = _recv_packet(sock, config.timeout_seconds)
            if packet_type != 9:
                return False, f"2.5.5 expected SUBACK (type 9), got type {packet_type}"
            if len(payload) < 2:
                return False, "2.5.5 SUBACK payload too short to contain packet identifier"
            suback_packet_id = int.from_bytes(payload[0:2], byteorder="big")
            if suback_packet_id != packet_identifier:
                return False, (
                    f"2.5.5 SUBACK packet ID mismatch: "
                    f"expected 0x{packet_identifier:04X}, got 0x{suback_packet_id:04X}"
                )
        finally:
            sock.close()
        return True, f"2.5.5 SUBACK packet ID 0x{packet_identifier:04X} matched SUBSCRIBE packet ID"
    except Exception as error:
        return False, f"2.5.5 failed: {error}"
    finally:
        stop_broker(process)


def run_2_5_6_subscribe_not_authorized(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-5-6")
    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
                "acl.rule": "deny,anonymous,subscribe,integration/subscribe/2-5-6/#",
            }
        )
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("sub-2-5-6"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.5.6 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x87)
        return True, "2.5.6 SUBACK returned 0x87 for not authorized subscribe"
    except Exception as error:
        return False, f"2.5.6 failed: {error}"
    finally:
        stop_broker(process)


# ─── 2.6 UNSUBSCRIBE ─────────────────────────────────────────────────────────

def run_2_6_1_unsubscribe_stops_message_delivery(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-6-1")
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-6-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.6.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-6-1"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publisher.publish(topic, b"before-unsub", qos=0)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"before-unsub", qos=0, retain=False)

            unsuback_codes = subscriber.unsubscribe(topic)
            if not unsuback_codes:
                return False, "2.6.1 UNSUBACK is empty"
            assert_reason_code(unsuback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher2:
                pub2_connack = publisher2.connect(host, port, client_id=_unique_client_id("pub2-2-6-1"), clean_start=True)
                assert_connack(pub2_connack, reason_code=0x00, session_present=False)
                publisher2.publish(topic, b"after-unsub", qos=0)

            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "2.6.1 no messages delivered after unsubscribe"
    except Exception as error:
        return False, f"2.6.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_6_2_unsubscribe_nonexistent_subscription(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-6-2")
    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("sub-2-6-2"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            unsuback_codes = client.unsubscribe(topic)
            if not unsuback_codes:
                return False, "2.6.2 UNSUBACK is empty"
            assert_reason_code(unsuback_codes[0], 0x11)
        return True, "2.6.2 UNSUBACK returned 0x11 for non-existent subscription"
    except Exception as error:
        return False, f"2.6.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_6_3_unsuback_packet_id_matches_unsubscribe(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-6-3")
    try:
        host, port, process = _start_isolated_broker()
        sock = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-2-6-3"))
        try:
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(topic, 0)],
                packet_identifier=10,
            )
            sock.sendall(subscribe_packet)
            _recv_packet(sock, config.timeout_seconds)  # consume SUBACK

            packet_identifier = 0x5678
            unsubscribe_packet = build_unsubscribe_packet(
                topic_filters=[topic],
                packet_identifier=packet_identifier,
            )
            sock.sendall(unsubscribe_packet)
            packet_type, _flags, payload = _recv_packet(sock, config.timeout_seconds)
            if packet_type != 11:
                return False, f"2.6.3 expected UNSUBACK (type 11), got type {packet_type}"
            if len(payload) < 2:
                return False, "2.6.3 UNSUBACK payload too short to contain packet identifier"
            unsuback_packet_id = int.from_bytes(payload[0:2], byteorder="big")
            if unsuback_packet_id != packet_identifier:
                return False, (
                    f"2.6.3 UNSUBACK packet ID mismatch: "
                    f"expected 0x{packet_identifier:04X}, got 0x{unsuback_packet_id:04X}"
                )
        finally:
            sock.close()
        return True, f"2.6.3 UNSUBACK packet ID 0x{packet_identifier:04X} matched UNSUBSCRIBE packet ID"
    except Exception as error:
        return False, f"2.6.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_6_4_unsubscribe_multiple_filters_per_filter_reason_codes(config) -> tuple[bool, str]:
    process = None
    topic_subscribed = _unique_topic("2-6-4-a")
    topic_not_subscribed = _unique_topic("2-6-4-b")
    try:
        host, port, process = _start_isolated_broker()
        sock = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-2-6-4"))
        try:
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(topic_subscribed, 0)],
                packet_identifier=20,
            )
            sock.sendall(subscribe_packet)
            _recv_packet(sock, config.timeout_seconds)  # consume SUBACK

            unsubscribe_packet = build_unsubscribe_packet(
                topic_filters=[topic_subscribed, topic_not_subscribed],
                packet_identifier=21,
            )
            sock.sendall(unsubscribe_packet)
            packet_type, _flags, payload = _recv_packet(sock, config.timeout_seconds)
            if packet_type != 11:
                return False, f"2.6.4 expected UNSUBACK (type 11), got type {packet_type}"
            reason_codes = _parse_suback_or_unsuback_reason_codes(payload)
            if len(reason_codes) != 2:
                return False, f"2.6.4 expected 2 UNSUBACK reason codes, got {len(reason_codes)}"
            assert_reason_code(reason_codes[0], 0x00)
            assert_reason_code(reason_codes[1], 0x11)
        finally:
            sock.close()
        return True, "2.6.4 UNSUBACK included per-filter reason codes for multi-filter UNSUBSCRIBE"
    except Exception as error:
        return False, f"2.6.4 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "subscribe/single_filter_suback_qos",
        "description": "2.5.1 Subscribe to single topic filter returns SUBACK with granted QoS",
        "run": run_2_5_1_subscribe_single_filter_suback_granted_qos,
    },
    {
        "name": "subscribe/multi_filter_suback_per_filter",
        "description": "2.5.2 Subscribe to multiple filters in single SUBSCRIBE returns SUBACK per filter",
        "run": run_2_5_2_subscribe_multiple_filters_suback_per_filter,
    },
    {
        "name": "subscribe/invalid_filter_suback_error",
        "description": "2.5.3 Subscribe to invalid topic filter returns SUBACK with error reason code",
        "run": run_2_5_3_subscribe_invalid_topic_filter_suback_error,
    },
    {
        "name": "subscribe/update_existing_subscription",
        "description": "2.5.4 Re-subscribe to same filter with different QoS updates the subscription",
        "run": run_2_5_4_subscribe_updates_existing_subscription,
    },
    {
        "name": "subscribe/suback_packet_id_matches",
        "description": "2.5.5 SUBACK Packet ID matches SUBSCRIBE Packet ID",
        "run": run_2_5_5_suback_packet_id_matches_subscribe,
    },
    {
        "name": "subscribe/not_authorized",
        "description": "2.5.6 Subscribe to denied topic filter returns SUBACK with 0x87",
        "run": run_2_5_6_subscribe_not_authorized,
    },
    {
        "name": "unsubscribe/no_more_messages",
        "description": "2.6.1 Unsubscribe from active subscription stops message delivery",
        "run": run_2_6_1_unsubscribe_stops_message_delivery,
    },
    {
        "name": "unsubscribe/nonexistent_subscription",
        "description": "2.6.2 Unsubscribe from non-existent subscription returns UNSUBACK with 0x11",
        "run": run_2_6_2_unsubscribe_nonexistent_subscription,
    },
    {
        "name": "unsubscribe/unsuback_packet_id_matches",
        "description": "2.6.3 UNSUBACK Packet ID matches UNSUBSCRIBE Packet ID",
        "run": run_2_6_3_unsuback_packet_id_matches_unsubscribe,
    },
    {
        "name": "unsubscribe/multi_filter_per_filter_reason_codes",
        "description": "2.6.4 Unsubscribe from multiple filters in single packet returns per-filter reason codes",
        "run": run_2_6_4_unsubscribe_multiple_filters_per_filter_reason_codes,
    },
]
