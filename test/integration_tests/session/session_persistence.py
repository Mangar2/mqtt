"""Integration tests for session persistence section 6.1 to 6.3."""

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

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/session/{prefix}/{uuid.uuid4().hex}"


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


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _new_disconnect_properties(**values):
    properties = Properties(PacketTypes.DISCONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _new_publish_properties(**values):
    properties = Properties(PacketTypes.PUBLISH)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _create_persistent_subscription(
    host: str,
    port: int,
    timeout_seconds: float,
    *,
    client_id: str,
    topic: str,
    session_expiry_seconds: int,
    subscription_qos: int = 1,
) -> None:
    with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
        connack = subscriber.connect(
            host,
            port,
            client_id=client_id,
            clean_start=True,
            properties=_new_connect_properties(SessionExpiryInterval=session_expiry_seconds),
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        suback_codes = subscriber.subscribe(topic, qos=subscription_qos)
        if not suback_codes:
            raise AssertionError("SUBACK is empty while creating persistent subscription")
        assert_reason_code(suback_codes[0], subscription_qos)


def _publish_single(
    host: str,
    port: int,
    timeout_seconds: float,
    *,
    topic: str,
    payload: bytes,
    qos: int,
    properties=None,
) -> int:
    with MqttClient(timeout_seconds=timeout_seconds) as publisher:
        connack = publisher.connect(
            host,
            port,
            client_id=_unique_client_id("session-publisher"),
            clean_start=True,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        return int(publisher.publish(topic, payload, qos=qos, properties=properties))


def _recv_exact(connection: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise RuntimeError("connection closed while receiving data")
        data.extend(chunk)
    return bytes(data)


def _decode_varint_from_socket(connection: socket.socket) -> tuple[int, bytes]:
    multiplier = 1
    value = 0
    encoded = bytearray()
    for _ in range(4):
        raw = _recv_exact(connection, 1)
        encoded.extend(raw)
        digit = raw[0]
        value += (digit & 0x7F) * multiplier
        if (digit & 0x80) == 0:
            return value, bytes(encoded)
        multiplier *= 128
    raise RuntimeError("malformed variable byte integer")


def _recv_packet(connection: socket.socket) -> tuple[int, bytes]:
    first_header = _recv_exact(connection, 1)[0]
    remaining_length, _ = _decode_varint_from_socket(connection)
    payload = _recv_exact(connection, remaining_length)
    return first_header, payload


def _encode_varint(value: int) -> bytes:
    if value < 0 or value > 268435455:
        raise ValueError("value out of range for MQTT varint")

    encoded = bytearray()
    rest = value
    while True:
        byte = rest % 128
        rest //= 128
        if rest > 0:
            byte |= 0x80
        encoded.append(byte)
        if rest == 0:
            break
    return bytes(encoded)


def _encode_utf8(text: str) -> bytes:
    payload = text.encode("utf-8")
    return len(payload).to_bytes(2, byteorder="big") + payload


def _build_connect_packet(client_id: str, clean_start: bool) -> bytes:
    connect_flags = 0x02 if clean_start else 0x00
    variable_header = bytearray()
    variable_header.extend(_encode_utf8("MQTT"))
    variable_header.append(5)
    variable_header.append(connect_flags)
    variable_header.extend((60).to_bytes(2, byteorder="big"))
    variable_header.extend(_encode_varint(0))

    payload = bytearray()
    payload.extend(_encode_utf8(client_id))

    remaining_length = len(variable_header) + len(payload)
    return bytes([0x10]) + _encode_varint(remaining_length) + bytes(variable_header) + bytes(payload)


def _build_puback_packet(packet_identifier: int) -> bytes:
    variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00]) + bytes([0x00])
    return bytes([0x40, len(variable_header)]) + variable_header


def _build_pubrec_packet(packet_identifier: int) -> bytes:
    variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00]) + bytes([0x00])
    return bytes([0x50, len(variable_header)]) + variable_header


def _build_pubcomp_packet(packet_identifier: int) -> bytes:
    variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00]) + bytes([0x00])
    return bytes([0x70, len(variable_header)]) + variable_header


def _raw_connect_and_expect_connack(host: str, port: int, client_id: str, clean_start: bool) -> socket.socket:
    connection = socket.create_connection((host, port), timeout=6.0)
    connection.settimeout(6.0)
    connection.sendall(_build_connect_packet(client_id=client_id, clean_start=clean_start))

    first_header, payload = _recv_packet(connection)
    packet_type = first_header >> 4
    if packet_type != 2:
        connection.close()
        raise RuntimeError(f"expected CONNACK packet, got type={packet_type}")
    if len(payload) < 2:
        connection.close()
        raise RuntimeError("CONNACK payload too short")

    reason_code = payload[1]
    if reason_code != 0x00:
        connection.close()
        raise RuntimeError(f"raw connect failed with CONNACK reason 0x{reason_code:02X}")
    return connection


def run_6_1_1_session_resume_sets_session_present(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("session-resume")
    topic = _unique_topic("session-present")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)

        return True, "6.1.1 reconnect with Clean Start=0 resumed session (Session Present=1)"
    except Exception as error:
        return False, f"6.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_6_1_2_subscriptions_survive_reconnect(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("subs-survive")
    topic = _unique_topic("subs-survive")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)

            publish_reason = _publish_single(
                host,
                port,
                config.timeout_seconds,
                topic=topic,
                payload=b"subscription-still-active",
                qos=1,
            )
            assert_reason_code(publish_reason, 0x00)

            messages = resumed.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(
                messages[0],
                topic=topic,
                payload=b"subscription-still-active",
                qos=1,
                retain=False,
            )

        return True, "6.1.2 subscription remained active after reconnect"
    except Exception as error:
        return False, f"6.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_6_1_3_offline_queued_messages_delivered(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("offline-queue")
    topic = _unique_topic("offline-queue")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
            subscription_qos=2,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"queued-while-offline",
            qos=1,
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"unexpected PUBACK reason 0x{publish_reason:02X} while queuing"

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)
            messages = resumed.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"queued-while-offline", qos=1, retain=False)

        return True, "6.1.3 offline QoS1 message was delivered on reconnect"
    except Exception as error:
        return False, f"6.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_6_1_4_qos1_inflight_retransmit_with_dup(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("inflight-qos1")
    topic = _unique_topic("inflight-qos1")
    first_connection = None
    second_connection = None

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
            subscription_qos=2,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"qos1-inflight",
            qos=1,
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"unexpected PUBACK reason 0x{publish_reason:02X} while queueing inflight QoS1"

        first_connection = _raw_connect_and_expect_connack(host, port, client_id=client_id, clean_start=False)
        first_header, first_payload = _recv_packet(first_connection)
        if (first_header >> 4) != 3:
            return False, f"expected PUBLISH on first reconnect, got type={(first_header >> 4)}"

        first_flags = first_header & 0x0F
        if (first_flags & 0x06) != 0x02:
            return False, f"expected QoS1 flags in first publish, got 0x{first_flags:01X}"

        first_connection.close()
        first_connection = None

        second_connection = _raw_connect_and_expect_connack(host, port, client_id=client_id, clean_start=False)
        second_header, second_payload = _recv_packet(second_connection)
        if (second_header >> 4) != 3:
            return False, f"expected retransmitted PUBLISH after reconnect, got type={(second_header >> 4)}"

        second_flags = second_header & 0x0F
        if (second_flags & 0x08) == 0:
            return False, "expected DUP=1 on retransmitted QoS1 message"

        topic_length = int.from_bytes(second_payload[0:2], byteorder="big")
        packet_id_offset = 2 + topic_length
        packet_identifier = int.from_bytes(second_payload[packet_id_offset : packet_id_offset + 2], byteorder="big")
        second_connection.sendall(_build_puback_packet(packet_identifier))

        return True, "6.1.4 QoS1 inflight message retransmitted after reconnect with DUP=1"
    except Exception as error:
        return False, f"6.1.4 failed: {error}"
    finally:
        if first_connection is not None:
            try:
                first_connection.close()
            except Exception:
                pass
        if second_connection is not None:
            try:
                second_connection.close()
            except Exception:
                pass
        stop_broker(process)


def run_6_1_5_qos2_inflight_state_resumed(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("inflight-qos2")
    topic = _unique_topic("inflight-qos2")
    first_connection = None
    second_connection = None

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
            subscription_qos=2,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"qos2-inflight",
            qos=2,
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"6.1.5 failed: unexpected PUBCOMP reason 0x{publish_reason:02X} while queueing inflight QoS2"

        first_connection = _raw_connect_and_expect_connack(host, port, client_id=client_id, clean_start=False)

        publish_header, publish_payload = _recv_packet(first_connection)
        if (publish_header >> 4) != 3:
            return False, f"6.1.5 failed: expected QoS2 PUBLISH, got type={(publish_header >> 4)}"

        publish_flags = publish_header & 0x0F
        if (publish_flags & 0x06) != 0x04:
            return False, f"6.1.5 failed: expected QoS2 flags in first publish, got 0x{publish_flags:01X}"

        topic_length = int.from_bytes(publish_payload[0:2], byteorder="big")
        packet_id_offset = 2 + topic_length
        packet_identifier = int.from_bytes(
            publish_payload[packet_id_offset : packet_id_offset + 2],
            byteorder="big",
        )

        first_connection.sendall(_build_pubrec_packet(packet_identifier))

        pubrel_header, pubrel_payload = _recv_packet(first_connection)
        if (pubrel_header >> 4) != 6:
            return False, f"6.1.5 failed: expected PUBREL after PUBREC, got type={(pubrel_header >> 4)}"

        first_connection.close()
        first_connection = None

        second_connection = _raw_connect_and_expect_connack(host, port, client_id=client_id, clean_start=False)
        resumed_header, resumed_payload = _recv_packet(second_connection)
        if (resumed_header >> 4) != 6:
            return False, f"6.1.5 failed: expected resumed PUBREL after reconnect, got type={(resumed_header >> 4)}"

        resumed_packet_identifier = int.from_bytes(resumed_payload[0:2], byteorder="big")
        if resumed_packet_identifier != packet_identifier:
            return False, "6.1.5 failed: resumed QoS2 state used unexpected packet identifier"

        second_connection.sendall(_build_pubcomp_packet(resumed_packet_identifier))

        return True, "6.1.5 QoS2 inflight state resumed and completed after reconnect"
    except Exception as error:
        return False, f"6.1.5 failed: {error}"
    finally:
        if first_connection is not None:
            try:
                first_connection.close()
            except Exception:
                pass
        if second_connection is not None:
            try:
                second_connection.close()
            except Exception:
                pass
        stop_broker(process)


def run_6_2_1_session_expiry_zero_deletes_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("expiry-zero")
    topic = _unique_topic("expiry-zero")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=0,
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=False)
            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.2.1 session expiry=0 deleted session on disconnect"
    except Exception as error:
        return False, f"6.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_6_2_2_session_expiry_elapsed_deletes_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("expiry-elapsed")
    topic = _unique_topic("expiry-elapsed")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=1,
        )

        time.sleep(2.0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=False)
            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.2.2 expired session was deleted after expiry interval elapsed"
    except Exception as error:
        return False, f"6.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_6_2_3_reconnect_after_expiry_is_fresh_session(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("expiry-fresh")
    topic = _unique_topic("expiry-fresh")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=1,
        )

        time.sleep(2.0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=False)

            publish_reason = _publish_single(
                host,
                port,
                config.timeout_seconds,
                topic=topic,
                payload=b"fresh-session-no-subscription",
                qos=1,
            )
            if publish_reason not in (0x00, 0x10):
                return False, f"unexpected PUBACK reason 0x{publish_reason:02X}"

            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.2.3 reconnect after expiry produced fresh session"
    except Exception as error:
        return False, f"6.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_6_2_4_disconnect_override_changes_expiry(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("expiry-override")
    topic = _unique_topic("expiry-override")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            connack = client.connect(
                host,
                port,
                client_id=client_id,
                clean_start=True,
                properties=_new_connect_properties(SessionExpiryInterval=120),
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = client.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK is empty in disconnect override setup"
            assert_reason_code(suback_codes[0], 0x01)
            client.disconnect(properties=_new_disconnect_properties(SessionExpiryInterval=0))

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=False)
            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.2.4 DISCONNECT Session Expiry override was applied"
    except Exception as error:
        return False, f"6.2.4 failed: {error}"
    finally:
        stop_broker(process)


def run_6_3_1_qos1_offline_message_queued(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("queue-qos1")
    topic = _unique_topic("queue-qos1")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"offline-qos1",
            qos=1,
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"unexpected PUBACK reason 0x{publish_reason:02X}"

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)
            messages = resumed.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=b"offline-qos1", qos=1, retain=False)

        return True, "6.3.1 offline QoS1 message was queued and delivered"
    except Exception as error:
        return False, f"6.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_6_3_2_qos2_offline_message_queued(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("queue-qos2")
    topic = _unique_topic("queue-qos2")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
            subscription_qos=2,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"offline-qos2",
            qos=2,
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"unexpected PUBCOMP reason 0x{publish_reason:02X}"

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)
            messages = resumed.collect_messages(count=1, timeout=config.timeout_seconds)
            if int(messages[0].qos) != 2:
                return False, f"expected QoS2 delivery, got QoS{int(messages[0].qos)}"
            assert_message(messages[0], topic=topic, payload=b"offline-qos2", qos=2, retain=False)

        return True, "6.3.2 offline QoS2 message was queued and delivered"
    except Exception as error:
        return False, f"6.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_6_3_3_qos0_not_queued_offline(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("queue-qos0")
    topic = _unique_topic("queue-qos0")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"offline-qos0",
            qos=0,
        )
        assert_reason_code(publish_reason, 0x00)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)
            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.3.3 offline QoS0 message was not queued"
    except Exception as error:
        return False, f"6.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_6_3_4_queue_limit_enforced_drop_oldest(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("queue-limit")
    topic = _unique_topic("queue-limit")

    try:
        host, port, process = _start_isolated_broker({"broker.max_queued_messages": 2})
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        for payload in (b"msg-1", b"msg-2", b"msg-3"):
            publish_reason = _publish_single(
                host,
                port,
                config.timeout_seconds,
                topic=topic,
                payload=payload,
                qos=1,
            )
            if publish_reason not in (0x00, 0x10):
                return False, f"unexpected PUBACK reason 0x{publish_reason:02X} while queueing"

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)

            messages = resumed.collect_messages(count=2, timeout=config.timeout_seconds)
            payloads = [bytes(message.payload) for message in messages]
            if payloads != [b"msg-2", b"msg-3"]:
                return False, (
                    "6.3.4 expected oldest message to be dropped at queue limit; "
                    f"received payloads={payloads!r}"
                )

        return True, "6.3.4 queue size limit enforced by dropping oldest messages"
    except Exception as error:
        return False, f"6.3.4 failed: {error}"
    finally:
        stop_broker(process)


def run_6_3_5_message_expiry_applied_to_offline_queue(config) -> tuple[bool, str]:
    process = None
    client_id = _unique_client_id("queue-expiry")
    topic = _unique_topic("queue-expiry")

    try:
        host, port, process = _start_isolated_broker()
        _create_persistent_subscription(
            host,
            port,
            config.timeout_seconds,
            client_id=client_id,
            topic=topic,
            session_expiry_seconds=120,
        )

        publish_reason = _publish_single(
            host,
            port,
            config.timeout_seconds,
            topic=topic,
            payload=b"will-expire",
            qos=1,
            properties=_new_publish_properties(MessageExpiryInterval=1),
        )
        if publish_reason not in (0x00, 0x10):
            return False, f"unexpected PUBACK reason 0x{publish_reason:02X} while queueing expiring message"

        time.sleep(2.0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed:
            connack = resumed.connect(host, port, client_id=client_id, clean_start=False)
            assert_connack(connack, reason_code=0x00, session_present=True)
            assert_no_message(resumed, timeout=min(1.5, config.timeout_seconds))

        return True, "6.3.5 expired offline message was not delivered after reconnect"
    except Exception as error:
        return False, f"6.3.5 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "session/persistence/session_resume_sets_session_present",
        "description": "6.1.1 Disconnect (Session Expiry > 0), reconnect Clean Start = 0 -> Session Present = 1",
        "run": run_6_1_1_session_resume_sets_session_present,
    },
    {
        "name": "session/persistence/subscriptions_survive_reconnect",
        "description": "6.1.2 Subscriptions survive across reconnect",
        "run": run_6_1_2_subscriptions_survive_reconnect,
    },
    {
        "name": "session/persistence/offline_queued_messages_delivered",
        "description": "6.1.3 Offline queued messages delivered after reconnect",
        "run": run_6_1_3_offline_queued_messages_delivered,
    },
    {
        "name": "session/persistence/qos1_inflight_retransmit_after_reconnect",
        "description": "6.1.4 QoS 1 inflight messages retransmitted after reconnect (DUP=1)",
        "run": run_6_1_4_qos1_inflight_retransmit_with_dup,
    },
    {
        "name": "session/persistence/qos2_inflight_state_resumed",
        "description": "6.1.5 QoS 2 inflight state resumed after reconnect",
        "run": run_6_1_5_qos2_inflight_state_resumed,
    },
    {
        "name": "session/persistence/session_expiry_zero_deletes_session",
        "description": "6.2.1 Session Expiry Interval = 0 -> session deleted on disconnect",
        "run": run_6_2_1_session_expiry_zero_deletes_session,
    },
    {
        "name": "session/persistence/session_expiry_elapsed_deletes_session",
        "description": "6.2.2 Session Expiry Interval elapsed -> session deleted, subscriptions removed",
        "run": run_6_2_2_session_expiry_elapsed_deletes_session,
    },
    {
        "name": "session/persistence/reconnect_after_expiry_fresh_session",
        "description": "6.2.3 Reconnect after session expired -> Session Present = 0, fresh session",
        "run": run_6_2_3_reconnect_after_expiry_is_fresh_session,
    },
    {
        "name": "session/persistence/disconnect_override_changes_expiry",
        "description": "6.2.4 Session Expiry Interval override on DISCONNECT -> new interval used",
        "run": run_6_2_4_disconnect_override_changes_expiry,
    },
    {
        "name": "session/persistence/qos1_offline_message_queued",
        "description": "6.3.1 QoS 1 message published while subscriber offline -> queued, delivered on reconnect",
        "run": run_6_3_1_qos1_offline_message_queued,
    },
    {
        "name": "session/persistence/qos2_offline_message_queued",
        "description": "6.3.2 QoS 2 message published while subscriber offline -> queued, delivered on reconnect",
        "run": run_6_3_2_qos2_offline_message_queued,
    },
    {
        "name": "session/persistence/qos0_not_queued_offline",
        "description": "6.3.3 QoS 0 messages NOT queued for offline sessions",
        "run": run_6_3_3_qos0_not_queued_offline,
    },
    {
        "name": "session/persistence/queue_limit_enforced_drop_oldest",
        "description": "6.3.4 Queue size limit enforced -> oldest messages dropped when full",
        "run": run_6_3_4_queue_limit_enforced_drop_oldest,
    },
    {
        "name": "session/persistence/message_expiry_applied_offline_queue",
        "description": "6.3.5 Message Expiry applied -> expired messages not delivered on reconnect",
        "run": run_6_3_5_message_expiry_applied_to_offline_queue,
    },
]
