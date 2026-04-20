"""Integration tests for Subscription Options (7.1 - 7.3)."""

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
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
SubscribeOptions = _mqtt_client_module.SubscribeOptions
build_connect_packet = _raw_tcp_module.build_connect_packet
build_subscribe_packet = _raw_tcp_module.build_subscribe_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string
encode_variable_byte_integer = _raw_tcp_module.encode_variable_byte_integer


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _topic(prefix: str) -> str:
    return f"integration/subscription/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker(overrides: dict[str, object] | None = None) -> tuple[str, int, object]:
    effective_overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    if overrides is not None:
        effective_overrides.update(overrides)
    process = start_broker(effective_overrides)
    return _broker_module.resolve_target_host("127.0.0.1"), int(effective_overrides["network.mqtt_port"]), process


def _try_collect_messages(client, max_count: int, timeout_seconds: float) -> list:
    deadline = time.monotonic() + timeout_seconds
    messages: list = []
    while len(messages) < max_count and time.monotonic() < deadline:
        remaining = max(0.05, deadline - time.monotonic())
        try:
            messages.extend(client.collect_messages(count=1, timeout=remaining))
        except TimeoutError:
            break
    return messages


def _extract_subscription_identifiers(message) -> list[int]:
    properties = getattr(message, "properties", None)
    if properties is None:
        return []

    value = getattr(properties, "SubscriptionIdentifier", None)
    if value is None:
        return []
    if isinstance(value, list):
        return [int(candidate) for candidate in value]
    return [int(value)]


def _recv_exact(tcp_socket: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = tcp_socket.recv(count - len(data))
        if chunk == b"":
            raise ConnectionError("socket closed")
        data.extend(chunk)
    return bytes(data)


def _try_recv_packet(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[int, int, bytes] | None:
    tcp_socket.settimeout(timeout_seconds)

    first = tcp_socket.recv(1)
    if first == b"":
        return None

    first_byte = int(first[0])
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
    return (first_byte >> 4) & 0x0F, first_byte & 0x0F, payload


def _raw_connect(host: str, port: int, timeout_seconds: float, client_id: str) -> socket.socket:
    tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
    try:
        connect_packet = build_connect_packet(payload=encode_utf8_string(client_id))
        tcp_socket.sendall(connect_packet)
        packet = _try_recv_packet(tcp_socket, timeout_seconds)
        if packet is None:
            raise RuntimeError("connection closed before CONNACK")

        packet_type, _flags, payload = packet
        if packet_type != 2:
            raise RuntimeError(f"expected CONNACK (type 2), got type {packet_type}")
        if len(payload) < 2 or int(payload[1]) != 0x00:
            reason_code = int(payload[1]) if len(payload) >= 2 else 0xFF
            raise RuntimeError(f"CONNECT rejected with reason 0x{reason_code:02X}")
        return tcp_socket
    except Exception:
        tcp_socket.close()
        raise


def _build_subscribe_with_properties(topic_filter: str, options_byte: int, packet_identifier: int, properties: bytes) -> bytes:
    variable_header = bytearray()
    variable_header.extend(packet_identifier.to_bytes(2, byteorder="big"))
    variable_header.extend(encode_variable_byte_integer(len(properties)))
    variable_header.extend(properties)

    payload = bytearray()
    payload.extend(encode_utf8_string(topic_filter))
    payload.append(options_byte & 0xFF)

    remaining = len(variable_header) + len(payload)
    fixed_header = bytes([(8 << 4) | 0x02]) + encode_variable_byte_integer(remaining)
    return fixed_header + bytes(variable_header) + bytes(payload)


def _assert_suback_granted(case_label: str, suback_codes: list[int], expected_qos: int) -> None:
    if not suback_codes:
        raise AssertionError(f"{case_label} SUBACK is empty")

    first_reason = int(suback_codes[0])
    if first_reason >= 0x80:
        raise AssertionError(
            f"{case_label} subscribe rejected by broker with reason 0x{first_reason:02X}"
        )

    assert_reason_code(first_reason, expected_qos)


def run_7_1_1_no_local_same_client_publish_not_received(config) -> tuple[bool, str]:
    process = None
    topic = _topic("7-1-1")
    payload = b"no-local-self"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            assert_connack(
                client.connect(host, port, client_id=_unique_client_id("sub-7-1-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )

            options = SubscribeOptions(qos=0, noLocal=True)
            suback_codes = client.subscribe(topic, options=options)
            _assert_suback_granted("7.1.1", suback_codes, expected_qos=0)

            publish_reason = client.publish(topic, payload, qos=0)
            assert_reason_code(publish_reason, 0x00)
            assert_no_message(client, timeout=min(1.5, config.timeout_seconds))

        return True, "7.1.1 No Local=1 suppressed self-published message"
    except Exception as error:
        return False, f"7.1.1 failed: {error}"
    finally:
        stop_broker(process)


def run_7_1_2_no_local_disabled_self_publish_received(config) -> tuple[bool, str]:
    process = None
    topic = _topic("7-1-2")
    payload = b"local-allowed"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as client:
            assert_connack(
                client.connect(host, port, client_id=_unique_client_id("sub-7-1-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )

            options = SubscribeOptions(qos=0, noLocal=False)
            suback_codes = client.subscribe(topic, options=options)
            _assert_suback_granted("7.1.2", suback_codes, expected_qos=0)

            publish_reason = client.publish(topic, payload, qos=0)
            assert_reason_code(publish_reason, 0x00)
            messages = client.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "7.1.2 No Local=0 delivered self-published message"
    except Exception as error:
        return False, f"7.1.2 failed: {error}"
    finally:
        stop_broker(process)


def run_7_1_3_no_local_other_publisher_received(config) -> tuple[bool, str]:
    process = None
    topic = _topic("7-1-3")
    payload = b"from-other-client"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-7-1-3"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )

            options = SubscribeOptions(qos=0, noLocal=True)
            suback_codes = subscriber.subscribe(topic, options=options)
            _assert_suback_granted("7.1.3", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-1-3"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publish_reason = publisher.publish(topic, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

        return True, "7.1.3 No Local=1 still delivered message from different client"
    except Exception as error:
        return False, f"7.1.3 failed: {error}"
    finally:
        stop_broker(process)


def run_7_2_1_subscription_identifier_forwarded(config) -> tuple[bool, str]:
    process = None
    topic = _topic("7-2-1")
    payload = b"with-subscription-identifier"
    subscription_identifier = 42

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-7-2-1"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = subscriber.subscribe(topic, qos=0, subscription_id=subscription_identifier)
            _assert_suback_granted("7.2.1", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-2-1"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(topic, payload, qos=0)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages[0], topic=topic, payload=payload, qos=0, retain=False)

            identifiers = _extract_subscription_identifiers(messages[0])
            if subscription_identifier not in identifiers:
                return False, (
                    "7.2.1 outbound PUBLISH missing Subscription Identifier "
                    f"{subscription_identifier}; got {identifiers}"
                )

        return True, "7.2.1 outbound PUBLISH included Subscription Identifier property"
    except Exception as error:
        return False, f"7.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_7_2_2_overlapping_subscriptions_include_both_identifiers(config) -> tuple[bool, str]:
    process = None
    run_id = uuid.uuid4().hex[:8]
    topic = f"integration/subscription/7-2-2/{run_id}/region/device/state"
    filter_broad = f"integration/subscription/7-2-2/{run_id}/region/+/state"
    filter_exact = topic
    payload = b"overlap-identifiers"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            assert_connack(
                subscriber.connect(host, port, client_id=_unique_client_id("sub-7-2-2"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = subscriber.subscribe(filter_broad, qos=0, subscription_id=11)
            _assert_suback_granted("7.2.2", suback_codes, expected_qos=0)
            suback_codes = subscriber.subscribe(filter_exact, qos=0, subscription_id=12)
            _assert_suback_granted("7.2.2", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-2-2"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(topic, payload, qos=0)

            messages = _try_collect_messages(subscriber, max_count=2, timeout_seconds=config.timeout_seconds)
            if not messages:
                return False, "7.2.2 no PUBLISH received for overlapping subscriptions"

            all_identifiers: list[int] = []
            for message in messages:
                all_identifiers.extend(_extract_subscription_identifiers(message))

            required = {11, 12}
            if not required.issubset(set(all_identifiers)):
                return False, (
                    "7.2.2 expected both Subscription Identifiers 11 and 12, "
                    f"got {all_identifiers}"
                )

        return True, "7.2.2 overlapping subscriptions delivered with both Subscription Identifiers"
    except Exception as error:
        return False, f"7.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_7_2_3_subscription_identifier_zero_protocol_error(config) -> tuple[bool, str]:
    process = None
    topic = _topic("7-2-3")

    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-7-2-3"))
        try:
            # Subscription Identifier property (0x0B) with value 0 is invalid in MQTT 5.
            invalid_properties = bytes([0x0B, 0x00])
            subscribe_packet = build_subscribe_packet(
                topic_filters=[(topic, 0)],
                packet_identifier=7,
                properties=invalid_properties,
            )
            tcp_socket.sendall(subscribe_packet)
            response = _try_recv_packet(tcp_socket, config.timeout_seconds)
        finally:
            tcp_socket.close()

        if response is None:
            return True, "7.2.3 invalid Subscription Identifier=0 caused immediate close"

        packet_type, _flags, payload = response
        if packet_type != 14:
            return False, f"7.2.3 expected DISCONNECT/close, got packet type {packet_type}"

        reason_code = int(payload[0]) if payload else 0x00
        if reason_code != 0x82:
            return False, f"7.2.3 expected Protocol Error 0x82, got 0x{reason_code:02X}"

        return True, "7.2.3 Subscription Identifier=0 rejected with Protocol Error 0x82"
    except Exception as error:
        return False, f"7.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_1_shared_subscription_delivers_to_one_member(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-1")
    shared_filter = f"$share/group-a/{base_topic}"
    payload = b"shared-single-delivery"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as shared_a, MqttClient(timeout_seconds=config.timeout_seconds) as shared_b:
            assert_connack(
                shared_a.connect(host, port, client_id=_unique_client_id("sub-7-3-1-a"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            assert_connack(
                shared_b.connect(host, port, client_id=_unique_client_id("sub-7-3-1-b"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = shared_a.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.1/shared-a", suback_codes, expected_qos=0)
            suback_codes = shared_b.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.1/shared-b", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-3-1"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(base_topic, payload, qos=0)

            received_a = len(_try_collect_messages(shared_a, max_count=1, timeout_seconds=min(1.5, config.timeout_seconds)))
            received_b = len(_try_collect_messages(shared_b, max_count=1, timeout_seconds=min(1.5, config.timeout_seconds)))
            if received_a + received_b != 1:
                return False, (
                    "7.3.1 expected exactly one shared recipient, "
                    f"got member-a={received_a}, member-b={received_b}"
                )

        return True, "7.3.1 shared subscription delivered message to exactly one group member"
    except Exception as error:
        return False, f"7.3.1 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_2_shared_subscription_load_distributed(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-2")
    shared_filter = f"$share/group-b/{base_topic}"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as shared_a, MqttClient(timeout_seconds=config.timeout_seconds) as shared_b:
            assert_connack(
                shared_a.connect(host, port, client_id=_unique_client_id("sub-7-3-2-a"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            assert_connack(
                shared_b.connect(host, port, client_id=_unique_client_id("sub-7-3-2-b"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = shared_a.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.2/shared-a", suback_codes, expected_qos=0)
            suback_codes = shared_b.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.2/shared-b", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-3-2"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                for index in range(10):
                    publisher.publish(base_topic, f"shared-{index}".encode("utf-8"), qos=0)

            messages_a = _try_collect_messages(shared_a, max_count=10, timeout_seconds=config.timeout_seconds)
            messages_b = _try_collect_messages(shared_b, max_count=10, timeout_seconds=config.timeout_seconds)

            total = len(messages_a) + len(messages_b)
            if total != 10:
                return False, f"7.3.2 expected 10 shared deliveries total, got {total}"
            if len(messages_a) == 0 or len(messages_b) == 0:
                return False, (
                    "7.3.2 expected load distribution across shared group members, "
                    f"got member-a={len(messages_a)}, member-b={len(messages_b)}"
                )

        return True, "7.3.2 shared subscription load was distributed across group members"
    except Exception as error:
        return False, f"7.3.2 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_3_shared_member_disconnect_remaining_receives(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-3")
    shared_filter = f"$share/group-c/{base_topic}"
    payload = b"after-member-disconnect"

    try:
        host, port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds) as shared_a, MqttClient(timeout_seconds=config.timeout_seconds) as shared_b:
            assert_connack(
                shared_a.connect(host, port, client_id=_unique_client_id("sub-7-3-3-a"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            assert_connack(
                shared_b.connect(host, port, client_id=_unique_client_id("sub-7-3-3-b"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = shared_a.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.3/shared-a", suback_codes, expected_qos=0)
            suback_codes = shared_b.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.3/shared-b", suback_codes, expected_qos=0)

            shared_a.disconnect()

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-3-3"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(base_topic, payload, qos=0)

            messages_b = shared_b.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(messages_b[0], topic=base_topic, payload=payload, qos=0, retain=False)

        return True, "7.3.3 remaining shared member received messages after peer disconnect"
    except Exception as error:
        return False, f"7.3.3 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_4_all_shared_members_disconnect_group_dissolved(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-4")
    shared_filter = f"$share/group-d/{base_topic}"

    try:
        host, port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds) as first_member:
            assert_connack(
                first_member.connect(host, port, client_id=_unique_client_id("sub-7-3-4-a"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = first_member.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.4/first-member", suback_codes, expected_qos=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as second_member:
            assert_connack(
                second_member.connect(host, port, client_id=_unique_client_id("sub-7-3-4-b"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = second_member.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.4/second-member", suback_codes, expected_qos=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            assert_connack(
                publisher.connect(host, port, client_id=_unique_client_id("pub-7-3-4"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            publisher.publish(base_topic, b"published-while-no-shared-members", qos=0)

        with MqttClient(timeout_seconds=config.timeout_seconds) as new_member:
            assert_connack(
                new_member.connect(host, port, client_id=_unique_client_id("sub-7-3-4-c"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            suback_codes = new_member.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.4/new-member", suback_codes, expected_qos=0)
            assert_no_message(new_member, timeout=min(1.5, config.timeout_seconds))

        return True, "7.3.4 shared group had no residual delivery after all members disconnected"
    except Exception as error:
        return False, f"7.3.4 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_5_non_shared_subscriber_gets_own_copy(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-5")
    shared_filter = f"$share/group-e/{base_topic}"
    payload = b"shared-plus-non-shared"

    try:
        host, port, process = _start_isolated_broker()
        with (
            MqttClient(timeout_seconds=config.timeout_seconds) as non_shared,
            MqttClient(timeout_seconds=config.timeout_seconds) as shared_a,
            MqttClient(timeout_seconds=config.timeout_seconds) as shared_b,
        ):
            assert_connack(
                non_shared.connect(host, port, client_id=_unique_client_id("sub-7-3-5-main"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            assert_connack(
                shared_a.connect(host, port, client_id=_unique_client_id("sub-7-3-5-a"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )
            assert_connack(
                shared_b.connect(host, port, client_id=_unique_client_id("sub-7-3-5-b"), clean_start=True),
                reason_code=0x00,
                session_present=False,
            )

            suback_codes = non_shared.subscribe(base_topic, qos=0)
            _assert_suback_granted("7.3.5/non-shared", suback_codes, expected_qos=0)
            suback_codes = shared_a.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.5/shared-a", suback_codes, expected_qos=0)
            suback_codes = shared_b.subscribe(shared_filter, qos=0)
            _assert_suback_granted("7.3.5/shared-b", suback_codes, expected_qos=0)

            with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
                assert_connack(
                    publisher.connect(host, port, client_id=_unique_client_id("pub-7-3-5"), clean_start=True),
                    reason_code=0x00,
                    session_present=False,
                )
                publisher.publish(base_topic, payload, qos=0)

            main_messages = non_shared.collect_messages(count=1, timeout=config.timeout_seconds)
            assert_message(main_messages[0], topic=base_topic, payload=payload, qos=0, retain=False)

            shared_count = len(_try_collect_messages(shared_a, max_count=1, timeout_seconds=min(1.5, config.timeout_seconds)))
            shared_count += len(_try_collect_messages(shared_b, max_count=1, timeout_seconds=min(1.5, config.timeout_seconds)))
            if shared_count != 1:
                return False, f"7.3.5 expected one shared recipient in addition to non-shared copy, got {shared_count}"

        return True, "7.3.5 non-shared subscriber received its own copy alongside shared group delivery"
    except Exception as error:
        return False, f"7.3.5 failed: {error}"
    finally:
        stop_broker(process)


def run_7_3_6_no_local_on_shared_subscription_protocol_error(config) -> tuple[bool, str]:
    process = None
    base_topic = _topic("7-3-6")
    shared_filter = f"$share/group-f/{base_topic}"

    try:
        host, port, process = _start_isolated_broker()
        tcp_socket = _raw_connect(host, port, config.timeout_seconds, _unique_client_id("raw-7-3-6"))
        try:
            # No Local is bit 2 in subscribe options; set it on a shared subscription.
            subscribe_packet = _build_subscribe_with_properties(
                topic_filter=shared_filter,
                options_byte=0x04,
                packet_identifier=9,
                properties=b"",
            )
            tcp_socket.sendall(subscribe_packet)
            response = _try_recv_packet(tcp_socket, config.timeout_seconds)
        finally:
            tcp_socket.close()

        if response is None:
            return True, "7.3.6 shared subscription with No Local caused immediate close"

        packet_type, _flags, payload = response
        if packet_type != 14:
            return False, f"7.3.6 expected DISCONNECT/close, got packet type {packet_type}"

        reason_code = int(payload[0]) if payload else 0x00
        if reason_code != 0x82:
            return False, f"7.3.6 expected Protocol Error 0x82, got 0x{reason_code:02X}"

        return True, "7.3.6 No Local on shared subscription rejected with Protocol Error 0x82"
    except Exception as error:
        return False, f"7.3.6 failed: {error}"
    finally:
        stop_broker(process)


TEST_CASES = [
    {
        "name": "subscription/no_local_same_client_publish_not_received",
        "description": "7.1.1 No Local=1 suppresses self-published message",
        "run": run_7_1_1_no_local_same_client_publish_not_received,
    },
    {
        "name": "subscription/no_local_disabled_self_publish_received",
        "description": "7.1.2 No Local=0 delivers self-published message",
        "run": run_7_1_2_no_local_disabled_self_publish_received,
    },
    {
        "name": "subscription/no_local_other_client_publish_received",
        "description": "7.1.3 No Local=1 still delivers message from other client",
        "run": run_7_1_3_no_local_other_publisher_received,
    },
    {
        "name": "subscription/subscription_identifier_forwarded",
        "description": "7.2.1 Subscription Identifier is forwarded in outbound PUBLISH",
        "run": run_7_2_1_subscription_identifier_forwarded,
    },
    {
        "name": "subscription/overlapping_subscriptions_both_identifiers",
        "description": "7.2.2 Overlapping subscriptions include both Subscription Identifiers",
        "run": run_7_2_2_overlapping_subscriptions_include_both_identifiers,
    },
    {
        "name": "subscription/subscription_identifier_zero_protocol_error",
        "description": "7.2.3 Subscription Identifier=0 is rejected as Protocol Error",
        "run": run_7_2_3_subscription_identifier_zero_protocol_error,
    },
    {
        "name": "subscription/shared_delivered_to_exactly_one_member",
        "description": "7.3.1 Shared subscription delivers each message to exactly one group member",
        "run": run_7_3_1_shared_subscription_delivers_to_one_member,
    },
    {
        "name": "subscription/shared_load_distributed_across_members",
        "description": "7.3.2 Shared subscription load is distributed across group members",
        "run": run_7_3_2_shared_subscription_load_distributed,
    },
    {
        "name": "subscription/shared_member_disconnect_remaining_receives",
        "description": "7.3.3 Remaining shared member receives messages after peer disconnect",
        "run": run_7_3_3_shared_member_disconnect_remaining_receives,
    },
    {
        "name": "subscription/shared_group_dissolved_after_all_disconnect",
        "description": "7.3.4 Shared group is dissolved when all members disconnect",
        "run": run_7_3_4_all_shared_members_disconnect_group_dissolved,
    },
    {
        "name": "subscription/non_shared_subscriber_gets_own_copy",
        "description": "7.3.5 Non-shared subscriber on same topic receives its own copy",
        "run": run_7_3_5_non_shared_subscriber_gets_own_copy,
    },
    {
        "name": "subscription/no_local_on_shared_protocol_error",
        "description": "7.3.6 No Local on shared subscription is rejected as Protocol Error",
        "run": run_7_3_6_no_local_on_shared_subscription_protocol_error,
    },
]