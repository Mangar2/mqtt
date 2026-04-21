"""Reusable assertion helpers for integration tests."""

from __future__ import annotations

import socket
import time
from typing import Any


def assert_reason_code(actual: int, expected: int) -> None:
    """Assert MQTT reason code equality with a readable message."""
    if int(actual) != int(expected):
        raise AssertionError(
            f"reason code mismatch: expected 0x{int(expected):02X}, got 0x{int(actual):02X}"
        )


def assert_connack(result: Any, reason_code: int, session_present: bool) -> None:
    """Verify core CONNACK fields."""
    if result is None:
        raise AssertionError("CONNACK result is missing")

    actual_reason = getattr(result, "reason_code", None)
    if actual_reason is None:
        raise AssertionError("CONNACK result has no reason_code")
    assert_reason_code(int(actual_reason), int(reason_code))

    actual_session_present = bool(getattr(result, "session_present", False))
    if actual_session_present != bool(session_present):
        raise AssertionError(
            "CONNACK session_present mismatch: "
            f"expected {bool(session_present)}, got {actual_session_present}"
        )


def _extract_property(properties: Any, property_id: Any) -> Any:
    if properties is None:
        return None

    if isinstance(property_id, str):
        return getattr(properties, property_id, None)

    for candidate in ("properties", "_properties"):
        container = getattr(properties, candidate, None)
        if isinstance(container, dict) and property_id in container:
            return container[property_id]

    if hasattr(properties, "get"):
        try:
            return properties.get(property_id)
        except Exception:
            return None
    return None


def assert_connack_property(result: Any, property_id: Any, expected_value: Any) -> None:
    """Verify a single CONNACK property value."""
    if result is None:
        raise AssertionError("CONNACK result is missing")

    actual_value = _extract_property(getattr(result, "properties", None), property_id)
    if actual_value != expected_value:
        raise AssertionError(
            "CONNACK property mismatch: "
            f"property={property_id!r}, expected={expected_value!r}, got={actual_value!r}"
        )


def assert_message(message: Any, topic: str, payload: bytes | str, qos: int, retain: bool) -> None:
    """Verify received PUBLISH message core fields."""
    if message is None:
        raise AssertionError("message is missing")

    expected_payload = payload.encode("utf-8") if isinstance(payload, str) else payload
    actual_topic = getattr(message, "topic", None)
    actual_payload = getattr(message, "payload", None)
    actual_qos = getattr(message, "qos", None)
    actual_retain = getattr(message, "retain", None)

    if actual_topic != topic:
        expected_topic_repr = repr(topic[:100] + "..." if len(topic) > 100 else topic)
        actual_topic_repr = repr(actual_topic[:100] + "..." if isinstance(actual_topic, str) and len(actual_topic) > 100 else actual_topic)
        raise AssertionError(f"message topic mismatch: expected {expected_topic_repr}, got {actual_topic_repr}")
    if actual_payload != expected_payload:
        expected_payload_repr = repr(expected_payload[:100] + b"..." if len(expected_payload) > 100 else expected_payload)
        actual_payload_repr = repr(actual_payload[:100] + b"..." if isinstance(actual_payload, bytes) and len(actual_payload) > 100 else actual_payload)
        raise AssertionError(
            f"message payload mismatch: expected {expected_payload_repr}, got {actual_payload_repr}"
        )
    if int(actual_qos) != int(qos):
        raise AssertionError(f"message qos mismatch: expected {int(qos)}, got {actual_qos}")
    if bool(actual_retain) != bool(retain):
        raise AssertionError(
            f"message retain mismatch: expected {bool(retain)}, got {bool(actual_retain)}"
        )


def assert_message_property(message: Any, property_id: Any, expected_value: Any) -> None:
    """Verify a single inbound PUBLISH property value."""
    if message is None:
        raise AssertionError("message is missing")

    actual_value = _extract_property(getattr(message, "properties", None), property_id)
    if actual_value != expected_value:
        raise AssertionError(
            "message property mismatch: "
            f"property={property_id!r}, expected={expected_value!r}, got={actual_value!r}"
        )


def assert_disconnected(client: Any, reason_code: int, timeout: float) -> None:
    """Verify broker-initiated disconnect with expected reason code."""
    if client is None or not hasattr(client, "wait_for_disconnect"):
        raise AssertionError("client does not support wait_for_disconnect(timeout)")

    disconnect_event = client.wait_for_disconnect(timeout)
    actual_reason = getattr(disconnect_event, "reason_code", None)
    if actual_reason is None:
        raise AssertionError("disconnect event has no reason_code")
    assert_reason_code(int(actual_reason), int(reason_code))


def assert_no_message(client: Any, timeout: float) -> None:
    """Verify that no message arrives during timeout window."""
    if client is None or not hasattr(client, "collect_messages"):
        raise AssertionError("client does not support collect_messages(count, timeout)")

    try:
        messages = client.collect_messages(count=1, timeout=timeout)
    except TimeoutError:
        return

    raise AssertionError(f"expected no message, but received {len(messages)} message(s)")


def assert_connection_closed(host: str, port: int, data: bytes, timeout: float) -> None:
    """Send bytes and verify broker closes the TCP connection."""
    closed = False
    with socket.create_connection((host, port), timeout=timeout) as tcp_socket:
        tcp_socket.settimeout(timeout)
        tcp_socket.sendall(data)

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                response = tcp_socket.recv(4096)
            except socket.timeout:
                continue
            if response == b"":
                closed = True
                break

    if not closed:
        raise AssertionError(
            f"expected broker to close connection after sending {len(data)} byte(s), but it stayed open"
        )
