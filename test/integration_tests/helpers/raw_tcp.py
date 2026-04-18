"""Raw TCP helper utilities for integration tests.

Provides low-level socket operations and MQTT packet builders for malformed
packet and robustness scenarios.
"""

from __future__ import annotations

import socket
import time


def encode_variable_byte_integer(value: int) -> bytes:
    """Encode MQTT variable byte integer."""
    if value < 0 or value > 268435455:
        raise ValueError("value out of range for MQTT variable byte integer")

    encoded = bytearray()
    remaining = value
    while True:
        current = remaining % 128
        remaining //= 128
        if remaining > 0:
            current |= 0x80
        encoded.append(current)
        if remaining == 0:
            break
    return bytes(encoded)


def encode_utf8_string(value: str) -> bytes:
    """Encode an MQTT UTF-8 string with two-byte length prefix."""
    encoded = value.encode("utf-8")
    if len(encoded) > 65535:
        raise ValueError("MQTT UTF-8 string exceeds 65535 bytes")
    return len(encoded).to_bytes(2, byteorder="big") + encoded


def build_packet(packet_type: int, flags: int, variable_header: bytes = b"", payload: bytes = b"") -> bytes:
    """Build a generic MQTT packet from fixed header, variable header and payload."""
    if packet_type < 1 or packet_type > 15:
        raise ValueError("packet_type must be in range 1..15")
    if flags < 0 or flags > 15:
        raise ValueError("flags must be in range 0..15")

    remaining_length = len(variable_header) + len(payload)
    fixed_header = bytes([(packet_type << 4) | flags]) + encode_variable_byte_integer(remaining_length)
    return fixed_header + variable_header + payload


def build_connect_packet(
    protocol_name: str = "MQTT",
    protocol_version: int = 5,
    connect_flags: int = 0x02,
    keepalive_seconds: int = 60,
    properties: bytes = b"",
    payload: bytes = b"",
) -> bytes:
    """Build an MQTT CONNECT packet with configurable protocol fields."""
    if protocol_version < 0 or protocol_version > 255:
        raise ValueError("protocol_version must be in range 0..255")
    if connect_flags < 0 or connect_flags > 255:
        raise ValueError("connect_flags must be in range 0..255")
    if keepalive_seconds < 0 or keepalive_seconds > 65535:
        raise ValueError("keepalive_seconds must be in range 0..65535")

    variable_header = b"".join(
        [
            encode_utf8_string(protocol_name),
            bytes([protocol_version]),
            bytes([connect_flags]),
            keepalive_seconds.to_bytes(2, byteorder="big"),
            encode_variable_byte_integer(len(properties)),
            properties,
        ]
    )
    return build_packet(packet_type=1, flags=0, variable_header=variable_header, payload=payload)


def build_publish_packet(
    topic: str,
    payload: bytes | str,
    qos: int = 0,
    retain: bool = False,
    dup: bool = False,
    packet_identifier: int | None = None,
    properties: bytes = b"",
) -> bytes:
    """Build an MQTT PUBLISH packet with configurable QoS and flags."""
    if qos < 0 or qos > 2:
        raise ValueError("qos must be 0, 1 or 2")
    if packet_identifier is not None and (packet_identifier < 1 or packet_identifier > 65535):
        raise ValueError("packet_identifier must be in range 1..65535")

    if isinstance(payload, str):
        payload_bytes = payload.encode("utf-8")
    else:
        payload_bytes = payload

    publish_flags = (0x08 if dup else 0) | ((qos & 0x03) << 1) | (0x01 if retain else 0)

    variable_header = bytearray()
    variable_header.extend(encode_utf8_string(topic))
    if qos > 0:
        identifier = 1 if packet_identifier is None else packet_identifier
        variable_header.extend(identifier.to_bytes(2, byteorder="big"))
    variable_header.extend(encode_variable_byte_integer(len(properties)))
    variable_header.extend(properties)

    return build_packet(
        packet_type=3,
        flags=publish_flags,
        variable_header=bytes(variable_header),
        payload=payload_bytes,
    )


def send_bytes(host: str, port: int, data: bytes, timeout_seconds: float = 1.0) -> bytes:
    """Open a TCP connection, send bytes and return all immediate response bytes."""
    with socket.create_connection((host, port), timeout=timeout_seconds) as tcp_socket:
        tcp_socket.settimeout(timeout_seconds)
        tcp_socket.sendall(data)

        chunks = bytearray()
        while True:
            try:
                received = tcp_socket.recv(4096)
            except socket.timeout:
                break

            if not received:
                break
            chunks.extend(received)
        return bytes(chunks)


def send_partial_connect(host: str, port: int, timeout_seconds: float = 1.0) -> dict[str, object]:
    """Send a truncated CONNECT packet and return broker reaction details."""
    connect_packet = build_connect_packet()
    truncated = connect_packet[: max(1, len(connect_packet) - 2)]

    response = b""
    closed_by_peer = False
    with socket.create_connection((host, port), timeout=timeout_seconds) as tcp_socket:
        tcp_socket.settimeout(timeout_seconds)
        tcp_socket.sendall(truncated)
        try:
            response = tcp_socket.recv(4096)
            closed_by_peer = response == b""
        except socket.timeout:
            closed_by_peer = False

    return {
        "sent_bytes": len(truncated),
        "response": response,
        "closed_by_peer": closed_by_peer,
    }


def open_idle_connection(host: str, port: int, duration_seconds: float) -> dict[str, object]:
    """Open a TCP connection, stay idle and report whether broker closes it."""
    start_time = time.monotonic()
    closed_by_peer = False

    with socket.create_connection((host, port), timeout=1.0) as tcp_socket:
        tcp_socket.settimeout(0.1)
        deadline = start_time + max(0.0, duration_seconds)
        while time.monotonic() < deadline:
            try:
                probe = tcp_socket.recv(1)
                if probe == b"":
                    closed_by_peer = True
                    break
            except socket.timeout:
                continue

    elapsed_seconds = time.monotonic() - start_time
    return {
        "closed_by_peer": closed_by_peer,
        "elapsed_seconds": elapsed_seconds,
    }


def send_and_expect_close(host: str, port: int, data: bytes, timeout: float) -> bool:
    """Send bytes and verify broker closes TCP connection within timeout."""
    with socket.create_connection((host, port), timeout=timeout) as tcp_socket:
        tcp_socket.settimeout(timeout)
        tcp_socket.sendall(data)

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                received = tcp_socket.recv(4096)
            except socket.timeout:
                continue
            if received == b"":
                return True
        return False


def flood_connections(host: str, port: int, count: int, timeout_seconds: float = 1.0) -> list[bool]:
    """Try to open count connections and report per-connection success/failure."""
    sockets: list[socket.socket] = []
    results: list[bool] = []
    try:
        for _ in range(max(0, count)):
            try:
                tcp_socket = socket.create_connection((host, port), timeout=timeout_seconds)
                sockets.append(tcp_socket)
                results.append(True)
            except OSError:
                results.append(False)
    finally:
        for tcp_socket in sockets:
            try:
                tcp_socket.close()
            except OSError:
                pass
    return results
