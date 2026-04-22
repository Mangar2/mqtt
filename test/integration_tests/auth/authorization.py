"""Integration tests for authorization section 11.1 to 11.2."""

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
build_publish_packet = _raw_tcp_module.build_publish_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/authz/{prefix}/{uuid.uuid4().hex}"


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


def _raw_connect(host: str, port: int, timeout_seconds: float, client_id: str) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
        tcp_socket.sendall(connect_packet)
        packet_type, _flags, payload = _recv_packet(tcp_socket, timeout_seconds)
        if packet_type != 2:
            raise RuntimeError(f"expected CONNACK packet type 2, got {packet_type}")
        if len(payload) < 2:
            raise RuntimeError("invalid CONNACK payload")
        reason_code = int(payload[1])
        if reason_code != 0x00:
            raise RuntimeError(f"CONNECT rejected with reason 0x{reason_code:02X}")
        return tcp_socket
    except Exception:
        tcp_socket.close()
        raise


def _extract_reason_code(payload: bytes) -> int:
    if len(payload) < 2:
        raise RuntimeError("ACK payload too short to include packet identifier")
    if len(payload) == 2:
        return 0x00
    return int(payload[2])


def _publish_qos1_raw_once(
    host: str,
    port: int,
    timeout_seconds: float,
    topic: str,
    payload: bytes,
    packet_identifier: int,
) -> tuple[int, int]:
    raw_socket = _raw_connect(host, port, timeout_seconds, _unique_client_id("raw-11-1"))
    try:
        publish_packet = build_publish_packet(
            topic=topic,
            payload=payload,
            qos=1,
            dup=False,
            packet_identifier=packet_identifier,
        )
        raw_socket.sendall(publish_packet)
        packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, timeout_seconds)
        return packet_type, _extract_reason_code(packet_payload)
    finally:
        raw_socket.close()


def _parse_suback_reason_codes(payload: bytes) -> list[int]:
    if len(payload) < 3:
        raise RuntimeError(f"SUBACK payload too short: {len(payload)} bytes")
    props_len = int(payload[2])
    reason_code_start = 3 + props_len
    if reason_code_start > len(payload):
        raise RuntimeError("SUBACK payload has invalid properties length")
    return list(payload[reason_code_start:])


def run_11_1_1_publish_allowed_topic_message_routed(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("11-1-1")
    payload = b"allowed-publish-routed"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-11-1-1"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "11.1.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-11-1-1"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=1, retain=False)

        return True, "11.1.1 allowed publish was accepted and routed"
    except Exception as error:
        return False, f"11.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_11_1_2_publish_denied_topic_returns_not_authorized(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("11-1-2/denied")

    try:
        host, port, process = _start_isolated_broker(
            {
                "acl.rule": "deny,anonymous,publish,integration/authz/11-1-2/denied/#",
            }
        )

        packet_type, publish_reason = _publish_qos1_raw_once(
            host,
            port,
            config.timeout_seconds,
            topic,
            b"expect-11-1-2-not-authorized",
            packet_identifier=1112,
        )
        if packet_type != 4:
            return False, f"11.1.2 expected PUBACK packet type 4, got {packet_type}"
        assert_reason_code(publish_reason, 0x87)

        return True, "11.1.2 denied publish returned PUBACK 0x87"
    except Exception as error:
        return False, f"11.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_11_1_3_publish_acl_wildcard_matches_correctly(config) -> tuple[bool, str]:
    process = None
    denied_topic = f"integration/authz/11-1-3/blocked/device-{uuid.uuid4().hex[:8]}/telemetry"
    allowed_topic = f"integration/authz/11-1-3/allowed/{uuid.uuid4().hex[:8]}"
    denied_payload = b"should-be-denied"
    allowed_payload = b"should-be-routed"

    try:
        host, port, process = _start_isolated_broker(
            {
                "acl.rule": "deny,anonymous,publish,integration/authz/11-1-3/blocked/+/telemetry",
            }
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-11-1-3"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe("integration/authz/11-1-3/#", qos=1)
            if not suback_codes:
                return False, "11.1.3 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            denied_packet_type, denied_reason = _publish_qos1_raw_once(
                host,
                port,
                config.timeout_seconds,
                denied_topic,
                denied_payload,
                packet_identifier=1113,
            )
            if denied_packet_type != 4:
                return False, f"11.1.3 expected PUBACK for denied publish, got packet type {denied_packet_type}"
            assert_reason_code(denied_reason, 0x87)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-11-1-3"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                allowed_reason = publisher.publish(allowed_topic, allowed_payload, qos=1)
                assert_reason_code(allowed_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=allowed_topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=allowed_topic, payload=allowed_payload, qos=1, retain=False)
            assert_no_message(subscriber, timeout=0.7)

        return True, "11.1.3 wildcard ACL denied matching topic and allowed non-matching topic"
    except Exception as error:
        return False, f"11.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_11_2_1_subscribe_allowed_topic_filter_suback_granted_qos(config) -> tuple[bool, str]:
    process = None
    topic_filter = _unique_topic("11-2-1")

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("sub-11-2-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(topic_filter, qos=1)
            if not suback_codes:
                return False, "11.2.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

        return True, "11.2.1 allowed subscribe returned granted QoS"
    except Exception as error:
        return False, f"11.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_11_2_2_subscribe_denied_topic_filter_suback_0x87(config) -> tuple[bool, str]:
    process = None
    denied_filter = _unique_topic("11-2-2/denied") + "/#"

    try:
        host, port, process = _start_isolated_broker(
            {
                "acl.rule": "deny,anonymous,subscribe,integration/authz/11-2-2/denied/#",
            }
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(host, port, client_id=_unique_client_id("sub-11-2-2"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(denied_filter, qos=1)
            if not suback_codes:
                return False, "11.2.2 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x87)

        return True, "11.2.2 denied subscribe returned SUBACK 0x87"
    except Exception as error:
        return False, f"11.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_11_2_3_subscribe_multiple_filters_mixed_suback_reason_codes(config) -> tuple[bool, str]:
    process = None
    allowed_filter = _unique_topic("11-2-3/allowed") + "/#"
    denied_filter = _unique_topic("11-2-3/denied") + "/#"

    try:
        host, port, process = _start_isolated_broker(
            {
                "acl.rule": "deny,anonymous,subscribe,integration/authz/11-2-3/denied/#",
            }
        )

        tcp_socket = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-11-2-3"))
        try:
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(allowed_filter, 1), (denied_filter, 1)],
                packet_identifier=1123,
            )
            tcp_socket.sendall(subscribe_packet)

            packet_type, _flags, payload = _recv_packet(tcp_socket, config.timeout_seconds)
            if packet_type != 9:
                return False, f"11.2.3 expected SUBACK packet type 9, got {packet_type}"

            reason_codes = _parse_suback_reason_codes(payload)
            if len(reason_codes) != 2:
                return False, f"11.2.3 expected 2 SUBACK reason codes, got {len(reason_codes)}"
            assert_reason_code(reason_codes[0], 0x01)
            assert_reason_code(reason_codes[1], 0x87)
        finally:
            tcp_socket.close()

        return True, "11.2.3 mixed subscribe permissions returned mixed SUBACK reason codes"
    except Exception as error:
        return False, f"11.2.3 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "authz/publish/allowed_topic_routed",
        "description": "11.1.1 Client publishes to allowed topic and message is routed",
        "run": run_11_1_1_publish_allowed_topic_message_routed,
    },
    {
        "name": "authz/publish/denied_topic_not_authorized",
        "description": "11.1.2 Client publishes to denied topic and receives PUBACK 0x87",
        "run": run_11_1_2_publish_denied_topic_returns_not_authorized,
    },
    {
        "name": "authz/publish/wildcard_acl_matches",
        "description": "11.1.3 ACL wildcard matching denies matching publish and allows non-matching publish",
        "run": run_11_1_3_publish_acl_wildcard_matches_correctly,
    },
    {
        "name": "authz/subscribe/allowed_filter_granted_qos",
        "description": "11.2.1 Client subscribes to allowed topic filter and receives granted QoS",
        "run": run_11_2_1_subscribe_allowed_topic_filter_suback_granted_qos,
    },
    {
        "name": "authz/subscribe/denied_filter_not_authorized",
        "description": "11.2.2 Client subscribes to denied topic filter and receives SUBACK 0x87",
        "run": run_11_2_2_subscribe_denied_topic_filter_suback_0x87,
    },
    {
        "name": "authz/subscribe/mixed_filters_mixed_reason_codes",
        "description": "11.2.3 Multiple filters with mixed permissions return mixed SUBACK reason codes",
        "run": run_11_2_3_subscribe_multiple_filters_mixed_suback_reason_codes,
    },
]