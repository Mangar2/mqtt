"""Integration tests for publish/subscribe core section 2.1 to 2.4."""

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
build_packet = _raw_tcp_module.build_packet
build_publish_packet = _raw_tcp_module.build_publish_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/publish/core/{prefix}/{uuid.uuid4().hex}"


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


def _connect_raw_publisher(host: str, port: int, timeout_seconds: float, client_id: str) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_payload = encode_utf8_string(client_id)
        connect_packet = build_connect_packet(payload=connect_payload)
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


def _expect_packet(packet_type: int, expected_type: int, description: str) -> None:
    if packet_type != expected_type:
        raise AssertionError(
            f"expected {description} packet type {expected_type}, got packet type {packet_type}"
        )


def _publish_qos1_with_raw_retransmit(host: str, port: int, timeout_seconds: float, topic: str, payload: bytes) -> tuple[int, int]:
    raw_socket = _connect_raw_publisher(
        host,
        port,
        timeout_seconds,
        _unique_client_id("raw-qos1"),
    )
    try:
        packet_identifier = 7
        first_publish = build_publish_packet(
            topic=topic,
            payload=payload,
            qos=1,
            dup=False,
            packet_identifier=packet_identifier,
        )
        raw_socket.sendall(first_publish)
        first_type, _first_flags, first_payload = _recv_packet(raw_socket, timeout_seconds)
        _expect_packet(first_type, 4, "PUBACK")
        first_reason = _extract_reason_code(first_payload)

        duplicate_publish = build_publish_packet(
            topic=topic,
            payload=payload,
            qos=1,
            dup=True,
            packet_identifier=packet_identifier,
        )
        raw_socket.sendall(duplicate_publish)
        second_type, _second_flags, second_payload = _recv_packet(raw_socket, timeout_seconds)
        _expect_packet(second_type, 4, "PUBACK")
        second_reason = _extract_reason_code(second_payload)

        return first_reason, second_reason
    finally:
        raw_socket.close()


def _publish_qos1_raw_once(
    host: str,
    port: int,
    timeout_seconds: float,
    topic: str,
    payload: bytes,
    packet_identifier: int,
) -> tuple[int, int]:
    raw_socket = _connect_raw_publisher(
        host,
        port,
        timeout_seconds,
        _unique_client_id("raw-qos1-once"),
    )
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


def _publish_qos2_raw_flow(
    host: str,
    port: int,
    timeout_seconds: float,
    topic: str,
    payload: bytes,
    packet_identifier: int,
) -> tuple[int, int]:
    raw_socket = _connect_raw_publisher(
        host,
        port,
        timeout_seconds,
        _unique_client_id("raw-qos2"),
    )
    try:
        publish_packet = build_publish_packet(
            topic=topic,
            payload=payload,
            qos=2,
            dup=False,
            packet_identifier=packet_identifier,
        )
        raw_socket.sendall(publish_packet)

        packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, timeout_seconds)
        _expect_packet(packet_type, 5, "PUBREC")
        pubrec_reason = _extract_reason_code(packet_payload)

        pubrel_variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00, 0x00])
        pubrel_packet = build_packet(packet_type=6, flags=2, variable_header=pubrel_variable_header)
        raw_socket.sendall(pubrel_packet)

        packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, timeout_seconds)
        _expect_packet(packet_type, 7, "PUBCOMP")
        pubcomp_reason = _extract_reason_code(packet_payload)

        return pubrec_reason, pubcomp_reason
    finally:
        raw_socket.close()


def run_2_1_1_qos0_publish_delivered(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-1-1")
    payload = b"qos0-delivery"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-1-1"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.1.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("pub-2-1-1"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=0, retain=False)

        return True, "2.1.1 QoS0 publish delivered to subscriber"
    except Exception as error:
        return False, f"2.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_1_2_qos0_no_ack_packets(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-1-2")

    try:
        host, port, process = _start_isolated_broker()
        raw_socket = _connect_raw_publisher(host, port, config.timeout_seconds, _unique_client_id("raw-2-1-2"))
        try:
            publish_packet = build_publish_packet(topic=topic, payload=b"no-ack-expected", qos=0)
            raw_socket.sendall(publish_packet)

            raw_socket.settimeout(min(0.8, max(0.2, config.timeout_seconds / 4)))
            try:
                packet_type, _packet_flags, _payload = _recv_packet(raw_socket, raw_socket.gettimeout())
            except socket.timeout:
                return True, "2.1.2 no ACK packet observed for QoS0 publish"

            return False, f"2.1.2 expected no ACK for QoS0 publish, but received packet type {packet_type}"
        finally:
            raw_socket.close()
    except Exception as error:
        return False, f"2.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_1_3_qos0_without_subscribers_dropped(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-1-3")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("pub-2-1-3"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"drop-if-no-subscriber", qos=0)
            assert_reason_code(publish_reason, 0x00)

        return True, "2.1.3 QoS0 publish to topic without subscribers completed without error"
    except Exception as error:
        return False, f"2.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_1_qos1_puback_sent(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-1")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-2-1"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.2.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-2-1"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, b"qos1-puback", qos=1)
                assert_reason_code(publish_reason, 0x00)

        return True, "2.2.1 QoS1 publish acknowledged with PUBACK success"
    except Exception as error:
        return False, f"2.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_2_qos1_delivery_respects_subscribed_qos(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-2")
    payload = b"qos1-to-qos0"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-2-2"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.2.2 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-2-2"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=0, retain=False)

        return True, "2.2.2 subscriber received message with QoS not higher than subscription QoS"
    except Exception as error:
        return False, f"2.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_3_qos1_retransmit_with_dup(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-3")
    payload = b"qos1-dup-retransmit"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-2-3"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.2.3 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            first_reason, second_reason = _publish_qos1_with_raw_retransmit(
                host,
                port,
                config.timeout_seconds,
                topic,
                payload,
            )
            assert_reason_code(first_reason, 0x00)
            assert_reason_code(second_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=1, retain=False)

        return True, "2.2.3 broker accepted QoS1 retransmit with DUP flag and acknowledged both attempts"
    except Exception as error:
        return False, f"2.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_4_qos1_puback_success_reason(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-4")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-2-4"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.2.4 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-2-4"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, b"qos1-success", qos=1)
                assert_reason_code(publish_reason, 0x00)

        return True, "2.2.4 PUBACK reason code was 0x00"
    except Exception as error:
        return False, f"2.2.4 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_5_qos1_no_matching_subscribers_reason(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-5")

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-2-5"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            publish_reason = publisher.publish(topic, b"no-matching-subscribers", qos=1)
            assert_reason_code(publish_reason, 0x10)

        return True, "2.2.5 PUBACK reason code was 0x10 when no subscribers matched"
    except Exception as error:
        return False, f"2.2.5 failed: {error}"
    finally:
        stop_broker(process)


def run_2_2_6_qos1_not_authorized_reason(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-2-6")

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
                "acl.rule": "deny,anonymous,publish,integration/publish/core/2-2-6/#",
            }
        )
        packet_type, publish_reason = _publish_qos1_raw_once(
            host,
            port,
            config.timeout_seconds,
            topic,
            b"expect-not-authorized",
            packet_identifier=31,
        )
        _expect_packet(packet_type, 4, "PUBACK")
        assert_reason_code(publish_reason, 0x87)

        return True, "2.2.6 PUBACK reason code was 0x87 for unauthorized QoS1 publish"
    except Exception as error:
        return False, f"2.2.6 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_1_qos2_full_flow(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-1")

    try:
        host, port, process = _start_isolated_broker()
        pubrec_reason, pubcomp_reason = _publish_qos2_raw_flow(
            host,
            port,
            config.timeout_seconds,
            topic,
            b"qos2-full-flow",
            packet_identifier=21,
        )
        assert_reason_code(pubrec_reason, 0x00)
        assert_reason_code(pubcomp_reason, 0x00)
        return True, "2.3.1 QoS2 flow completed: PUBLISH -> PUBREC -> PUBREL -> PUBCOMP"
    except Exception as error:
        return False, f"2.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_2_qos2_exactly_once_delivery(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-2")
    payload = b"qos2-once"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-3-2"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=2)
            if not suback_codes:
                return False, "2.3.2 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x02)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-3-2"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=2, retain=False)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "2.3.2 QoS2 publish delivered exactly once"
    except Exception as error:
        return False, f"2.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_3_qos2_duplicate_publish_handled_once(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-3")
    payload = b"qos2-duplicate"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-3-3"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=2)
            if not suback_codes:
                return False, "2.3.3 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x02)

            raw_socket = _connect_raw_publisher(host, port, config.timeout_seconds, _unique_client_id("raw-2-3-3"))
            try:
                packet_identifier = 41
                first_publish = build_publish_packet(
                    topic=topic,
                    payload=payload,
                    qos=2,
                    dup=False,
                    packet_identifier=packet_identifier,
                )
                raw_socket.sendall(first_publish)
                packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
                _expect_packet(packet_type, 5, "PUBREC")
                assert_reason_code(_extract_reason_code(packet_payload), 0x00)

                duplicate_publish = build_publish_packet(
                    topic=topic,
                    payload=payload,
                    qos=2,
                    dup=True,
                    packet_identifier=packet_identifier,
                )
                raw_socket.sendall(duplicate_publish)
                packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
                _expect_packet(packet_type, 5, "PUBREC")
                assert_reason_code(_extract_reason_code(packet_payload), 0x00)

                pubrel_variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00, 0x00])
                pubrel_packet = build_packet(packet_type=6, flags=2, variable_header=pubrel_variable_header)
                raw_socket.sendall(pubrel_packet)
                packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
                _expect_packet(packet_type, 7, "PUBCOMP")
                assert_reason_code(_extract_reason_code(packet_payload), 0x00)
            finally:
                raw_socket.close()

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=2, retain=False)
            assert_no_message(subscriber, timeout=min(1.5, config.timeout_seconds))

        return True, "2.3.3 duplicate QoS2 PUBLISH returned PUBREC again and was delivered once"
    except Exception as error:
        return False, f"2.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_4_qos2_pubrec_error_aborts_flow(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-4")

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
                "acl.rule": "deny,anonymous,publish,integration/publish/core/2-3-4/#",
            }
        )
        raw_socket = _connect_raw_publisher(host, port, config.timeout_seconds, _unique_client_id("raw-2-3-4"))
        try:
            publish_packet = build_publish_packet(
                topic=topic,
                payload=b"expect-pubrec-error",
                qos=2,
                dup=False,
                packet_identifier=51,
            )
            raw_socket.sendall(publish_packet)
            packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
            _expect_packet(packet_type, 5, "PUBREC")
            pubrec_reason = _extract_reason_code(packet_payload)
            assert_reason_code(pubrec_reason, 0x87)

            raw_socket.settimeout(min(1.0, max(0.2, config.timeout_seconds / 4)))
            try:
                next_type, _next_flags, _next_payload = _recv_packet(raw_socket, raw_socket.gettimeout())
            except socket.timeout:
                return True, "2.3.4 PUBREC error observed and broker sent no subsequent PUBREL"

            return False, f"2.3.4 expected flow abort after PUBREC error, but received packet type {next_type}"
        finally:
            raw_socket.close()
    except Exception as error:
        return False, f"2.3.4 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_5_qos2_pubrel_retransmission(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-5")

    try:
        host, port, process = _start_isolated_broker()
        raw_socket = _connect_raw_publisher(host, port, config.timeout_seconds, _unique_client_id("raw-2-3-5"))
        try:
            packet_identifier = 61
            publish_packet = build_publish_packet(
                topic=topic,
                payload=b"pubrel-retransmit",
                qos=2,
                dup=False,
                packet_identifier=packet_identifier,
            )
            raw_socket.sendall(publish_packet)
            packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
            _expect_packet(packet_type, 5, "PUBREC")
            assert_reason_code(_extract_reason_code(packet_payload), 0x00)

            pubrel_variable_header = packet_identifier.to_bytes(2, byteorder="big") + bytes([0x00, 0x00])
            first_pubrel = build_packet(packet_type=6, flags=2, variable_header=pubrel_variable_header)
            raw_socket.sendall(first_pubrel)
            packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
            _expect_packet(packet_type, 7, "PUBCOMP")
            assert_reason_code(_extract_reason_code(packet_payload), 0x00)

            second_pubrel = build_packet(packet_type=6, flags=2, variable_header=pubrel_variable_header)
            raw_socket.sendall(second_pubrel)
            packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
            _expect_packet(packet_type, 7, "PUBCOMP")
            assert_reason_code(_extract_reason_code(packet_payload), 0x00)

        finally:
            raw_socket.close()

        return True, "2.3.5 broker answered PUBREL retransmission with PUBCOMP"
    except Exception as error:
        return False, f"2.3.5 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_6_qos2_packet_identifier_released_after_pubcomp(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-6")

    try:
        host, port, process = _start_isolated_broker()
        packet_identifier = 71
        first_pubrec, first_pubcomp = _publish_qos2_raw_flow(
            host,
            port,
            config.timeout_seconds,
            topic,
            b"qos2-first",
            packet_identifier=packet_identifier,
        )
        assert_reason_code(first_pubrec, 0x00)
        assert_reason_code(first_pubcomp, 0x00)

        second_pubrec, second_pubcomp = _publish_qos2_raw_flow(
            host,
            port,
            config.timeout_seconds,
            topic,
            b"qos2-second-same-id",
            packet_identifier=packet_identifier,
        )
        assert_reason_code(second_pubrec, 0x00)
        assert_reason_code(second_pubcomp, 0x00)

        return True, "2.3.6 packet identifier reusable after PUBCOMP"
    except Exception as error:
        return False, f"2.3.6 failed: {error}"
    finally:
        stop_broker(process)


def run_2_3_7_qos2_not_authorized_reason(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-3-7")

    try:
        host, port, process = _start_isolated_broker(
            {
                "broker.allow_anonymous": True,
                "acl.rule": "deny,anonymous,publish,integration/publish/core/2-3-7/#",
            }
        )
        raw_socket = _connect_raw_publisher(host, port, config.timeout_seconds, _unique_client_id("raw-2-3-7"))
        try:
            publish_packet = build_publish_packet(
                topic=topic,
                payload=b"expect-not-authorized-qos2",
                qos=2,
                dup=False,
                packet_identifier=81,
            )
            raw_socket.sendall(publish_packet)
            packet_type, _packet_flags, packet_payload = _recv_packet(raw_socket, config.timeout_seconds)
            _expect_packet(packet_type, 5, "PUBREC")
            pubrec_reason = _extract_reason_code(packet_payload)
            assert_reason_code(pubrec_reason, 0x87)
        finally:
            raw_socket.close()

        return True, "2.3.7 PUBREC reason code was 0x87 for unauthorized QoS2 publish"
    except Exception as error:
        return False, f"2.3.7 failed: {error}"
    finally:
        stop_broker(process)


def run_2_4_1_qos2_to_qos1_downgrade(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-4-1")
    payload = b"qos2-to-qos1"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-4-1"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "2.4.1 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-4-1"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=1, retain=False)

        return True, "2.4.1 QoS2 publish delivered as QoS1 when subscriber max QoS is 1"
    except Exception as error:
        return False, f"2.4.1 failed: {error}"
    finally:
        stop_broker(process)


def run_2_4_2_qos2_to_qos0_downgrade(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-4-2")
    payload = b"qos2-to-qos0"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-4-2"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.4.2 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-4-2"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=0, retain=False)

        return True, "2.4.2 QoS2 publish delivered as QoS0 when subscriber max QoS is 0"
    except Exception as error:
        return False, f"2.4.2 failed: {error}"
    finally:
        stop_broker(process)


def run_2_4_3_qos1_to_qos0_downgrade(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-4-3")
    payload = b"qos1-to-qos0"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-4-3"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "2.4.3 SUBACK is empty"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-4-3"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=0, retain=False)

        return True, "2.4.3 QoS1 publish delivered as QoS0 when subscriber max QoS is 0"
    except Exception as error:
        return False, f"2.4.3 failed: {error}"
    finally:
        stop_broker(process)


def run_2_4_4_server_maximum_qos_respected(config) -> tuple[bool, str]:
    process = None
    topic = _unique_topic("2-4-4")
    payload = b"server-maximum-qos"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            sub_connack = subscriber.connect(host, port, client_id=_unique_client_id("sub-2-4-4"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)

            connack_properties = getattr(sub_connack, "properties", None)
            maximum_qos = getattr(connack_properties, "MaximumQoS", None)
            max_qos_int = 2 if maximum_qos is None else int(maximum_qos)
            if max_qos_int not in (0, 1, 2):
                return False, f"2.4.4 invalid effective MaximumQoS value {max_qos_int}"
            if maximum_qos is not None and max_qos_int == 2:
                return False, "2.4.4 MaximumQoS property must be 0 or 1 when present"

            suback_codes = subscriber.subscribe(topic, qos=2)
            if not suback_codes:
                return False, "2.4.4 SUBACK is empty"
            granted_qos = int(suback_codes[0])
            if granted_qos > max_qos_int:
                return False, f"2.4.4 broker granted QoS {granted_qos} above MaximumQoS {max_qos_int}"

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                pub_connack = publisher.connect(host, port, client_id=_unique_client_id("pub-2-4-4"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)

                requested_qos = 2
                publish_reason = publisher.publish(topic, payload, qos=requested_qos)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            expected_qos = min(requested_qos, granted_qos, max_qos_int)
            assert_message(messages, topic=topic, payload=payload, qos=expected_qos, retain=False)

        return True, "2.4.4 outbound delivery respected server MaximumQoS from CONNACK"
    except Exception as error:
        return False, f"2.4.4 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "publish/qos0/publish_delivered",
        "description": "2.1.1 Publish QoS 0 is delivered to subscriber",
        "run": run_2_1_1_qos0_publish_delivered,
    },
    {
        "name": "publish/qos0/no_ack_packets",
        "description": "2.1.2 Publish QoS 0 exchanges no ACK packets",
        "run": run_2_1_2_qos0_no_ack_packets,
    },
    {
        "name": "publish/qos0/no_subscribers_dropped",
        "description": "2.1.3 Publish QoS 0 with no subscribers is dropped without error",
        "run": run_2_1_3_qos0_without_subscribers_dropped,
    },
    {
        "name": "publish/qos1/puback_sent",
        "description": "2.2.1 Publish QoS 1 returns PUBACK",
        "run": run_2_2_1_qos1_puback_sent,
    },
    {
        "name": "publish/qos1/delivery_respects_subscribed_qos",
        "description": "2.2.2 Publish QoS 1 is delivered with QoS <= subscribed QoS",
        "run": run_2_2_2_qos1_delivery_respects_subscribed_qos,
    },
    {
        "name": "publish/qos1/retransmit_dup",
        "description": "2.2.3 Publish QoS 1 retransmit with DUP flag is handled",
        "run": run_2_2_3_qos1_retransmit_with_dup,
    },
    {
        "name": "publish/qos1/puback_success_reason",
        "description": "2.2.4 Publish QoS 1 PUBACK reason code is 0x00",
        "run": run_2_2_4_qos1_puback_success_reason,
    },
    {
        "name": "publish/qos1/no_matching_subscribers_reason",
        "description": "2.2.5 Publish QoS 1 with no subscribers returns 0x10",
        "run": run_2_2_5_qos1_no_matching_subscribers_reason,
    },
    {
        "name": "publish/qos1/not_authorized_reason",
        "description": "2.2.6 Publish QoS 1 not authorized returns 0x87",
        "run": run_2_2_6_qos1_not_authorized_reason,
    },
    {
        "name": "publish/qos2/full_flow",
        "description": "2.3.1 Full QoS 2 flow completes",
        "run": run_2_3_1_qos2_full_flow,
    },
    {
        "name": "publish/qos2/exactly_once_delivery",
        "description": "2.3.2 Publish QoS 2 is delivered exactly once",
        "run": run_2_3_2_qos2_exactly_once_delivery,
    },
    {
        "name": "publish/qos2/duplicate_publish_handled_once",
        "description": "2.3.3 Duplicate QoS 2 PUBLISH gets PUBREC again and is not delivered twice",
        "run": run_2_3_3_qos2_duplicate_publish_handled_once,
    },
    {
        "name": "publish/qos2/pubrec_error_aborts",
        "description": "2.3.4 PUBREC error aborts QoS 2 flow without PUBREL",
        "run": run_2_3_4_qos2_pubrec_error_aborts_flow,
    },
    {
        "name": "publish/qos2/pubrel_retransmission",
        "description": "2.3.5 PUBREL retransmission after PUBREC is handled",
        "run": run_2_3_5_qos2_pubrel_retransmission,
    },
    {
        "name": "publish/qos2/packet_id_released",
        "description": "2.3.6 PUBCOMP completes flow and releases packet identifier",
        "run": run_2_3_6_qos2_packet_identifier_released_after_pubcomp,
    },
    {
        "name": "publish/qos2/not_authorized_reason",
        "description": "2.3.7 Publish QoS 2 not authorized returns 0x87",
        "run": run_2_3_7_qos2_not_authorized_reason,
    },
    {
        "name": "publish/qos_downgrade/qos2_to_qos1",
        "description": "2.4.1 Publish QoS 2 to subscriber max QoS 1 is delivered as QoS 1",
        "run": run_2_4_1_qos2_to_qos1_downgrade,
    },
    {
        "name": "publish/qos_downgrade/qos2_to_qos0",
        "description": "2.4.2 Publish QoS 2 to subscriber max QoS 0 is delivered as QoS 0",
        "run": run_2_4_2_qos2_to_qos0_downgrade,
    },
    {
        "name": "publish/qos_downgrade/qos1_to_qos0",
        "description": "2.4.3 Publish QoS 1 to subscriber max QoS 0 is delivered as QoS 0",
        "run": run_2_4_3_qos1_to_qos0_downgrade,
    },
    {
        "name": "publish/qos_downgrade/server_maximum_qos_respected",
        "description": "2.4.4 Server Maximum QoS in CONNACK is respected for outbound delivery",
        "run": run_2_4_4_server_maximum_qos_respected,
    },
]
