"""Integration tests for section 14 (WebSocket transport)."""

from __future__ import annotations

import base64
import importlib.util
from pathlib import Path
import socket
import struct
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
build_publish_packet = _raw_tcp_module.build_publish_packet
encode_utf8_string = _raw_tcp_module.encode_utf8_string


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/websocket/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _start_isolated_broker(overrides: dict[str, object] | None = None):
    effective_overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": _find_free_port(),
        "broker.allow_anonymous": True,
    }
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return "127.0.0.1", int(effective_overrides["network.mqtt_port"]), int(effective_overrides["network.ws_port"]), process


def _read_http_headers(tcp_socket: socket.socket, timeout_seconds: float) -> bytes:
    tcp_socket.settimeout(timeout_seconds)
    response_buffer = bytearray()
    while b"\r\n\r\n" not in response_buffer:
        chunk = tcp_socket.recv(4096)
        if not chunk:
            break
        response_buffer.extend(chunk)
        if len(response_buffer) > 65536:
            raise RuntimeError("HTTP response too large")
    return bytes(response_buffer)


def _build_handshake_request(host: str, port: int, include_protocol: bool) -> bytes:
    key_bytes = uuid.uuid4().bytes + uuid.uuid4().bytes
    websocket_key = base64.b64encode(key_bytes).decode("ascii")

    lines = [
        "GET /mqtt HTTP/1.1",
        f"Host: {host}:{port}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        f"Sec-WebSocket-Key: {websocket_key}",
        "Sec-WebSocket-Version: 13",
    ]
    if include_protocol:
        lines.append("Sec-WebSocket-Protocol: mqtt")

    return ("\r\n".join(lines) + "\r\n\r\n").encode("ascii")


def _websocket_handshake(
    host: str,
    ws_port: int,
    timeout_seconds: float,
    include_protocol: bool,
) -> tuple[socket.socket, str]:
    tcp_socket = socket.create_connection((host, ws_port), timeout=timeout_seconds)
    request = _build_handshake_request(host, ws_port, include_protocol=include_protocol)
    tcp_socket.sendall(request)
    response = _read_http_headers(tcp_socket, timeout_seconds)
    return tcp_socket, response.decode("latin-1", errors="replace")


def _encode_ws_frame(opcode: int, payload: bytes, *, fin: bool = True, masked: bool = True) -> bytes:
    first_byte = (0x80 if fin else 0x00) | (opcode & 0x0F)
    frame = bytearray([first_byte])

    payload_length = len(payload)
    mask_bit = 0x80 if masked else 0x00
    if payload_length <= 125:
        frame.append(mask_bit | payload_length)
    elif payload_length <= 0xFFFF:
        frame.append(mask_bit | 126)
        frame.extend(struct.pack("!H", payload_length))
    else:
        frame.append(mask_bit | 127)
        frame.extend(struct.pack("!Q", payload_length))

    if masked:
        masking_key = b"\x11\x22\x33\x44"
        frame.extend(masking_key)
        frame.extend(
            payload[index] ^ masking_key[index % 4]
            for index in range(payload_length)
        )
    else:
        frame.extend(payload)

    return bytes(frame)


def _read_exact(tcp_socket: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        received = tcp_socket.recv(length - len(chunks))
        if not received:
            raise EOFError("socket closed while reading frame")
        chunks.extend(received)
    return bytes(chunks)


def _read_ws_frame(tcp_socket: socket.socket, timeout_seconds: float) -> tuple[int, bool, bytes]:
    tcp_socket.settimeout(timeout_seconds)
    header = _read_exact(tcp_socket, 2)
    first_byte = header[0]
    second_byte = header[1]

    fin = (first_byte & 0x80) != 0
    opcode = first_byte & 0x0F
    masked = (second_byte & 0x80) != 0

    payload_length = second_byte & 0x7F
    if payload_length == 126:
        payload_length = struct.unpack("!H", _read_exact(tcp_socket, 2))[0]
    elif payload_length == 127:
        payload_length = struct.unpack("!Q", _read_exact(tcp_socket, 8))[0]

    masking_key = b""
    if masked:
        masking_key = _read_exact(tcp_socket, 4)

    payload = bytearray(_read_exact(tcp_socket, payload_length))
    if masked:
        for index in range(payload_length):
            payload[index] ^= masking_key[index % 4]

    return opcode, fin, bytes(payload)


def _decode_connack_reason_code(packet_bytes: bytes) -> int:
    if len(packet_bytes) < 4:
        raise AssertionError(f"expected full CONNACK packet, got {len(packet_bytes)} byte(s)")
    if packet_bytes[0] != 0x20:
        raise AssertionError(f"expected CONNACK packet type 0x20, got 0x{packet_bytes[0]:02X}")
    return int(packet_bytes[3])


def run_14_1_1_valid_upgrade_established(config) -> tuple[bool, str]:
    process = None
    tcp_socket = None
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        tcp_socket, handshake_text = _websocket_handshake(
            host,
            ws_port,
            timeout_seconds=config.timeout_seconds,
            include_protocol=True,
        )

        if "101 Switching Protocols" not in handshake_text:
            return False, f"expected 101 Switching Protocols, got: {handshake_text!r}"
        if "Sec-WebSocket-Accept:" not in handshake_text:
            return False, "missing Sec-WebSocket-Accept header in handshake response"

        return True, "14.1.1 valid HTTP upgrade established WebSocket transport"
    except Exception as error:
        return False, f"14.1.1 failed: {error}"
    finally:
        if tcp_socket is not None:
            tcp_socket.close()
        stop_broker(process)


def run_14_1_2_missing_subprotocol_rejected(config) -> tuple[bool, str]:
    process = None
    tcp_socket = None
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        tcp_socket, handshake_text = _websocket_handshake(
            host,
            ws_port,
            timeout_seconds=config.timeout_seconds,
            include_protocol=False,
        )

        if "101 Switching Protocols" in handshake_text:
            return (
                False,
                "14.1.2 expected rejection without Sec-WebSocket-Protocol: mqtt, "
                "but broker accepted upgrade (this is the intentional red test)",
            )

        return True, "14.1.2 missing Sec-WebSocket-Protocol was rejected"
    except Exception as error:
        return False, f"14.1.2 failed: {error}"
    finally:
        if tcp_socket is not None:
            tcp_socket.close()
        stop_broker(process)


def run_14_1_3_invalid_upgrade_http_400(config) -> tuple[bool, str]:
    process = None
    tcp_socket = None
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        tcp_socket = socket.create_connection((host, ws_port), timeout=config.timeout_seconds)
        invalid_request = (
            "GET /mqtt HTTP/1.1\r\n"
            f"Host: {host}:{ws_port}\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        ).encode("ascii")
        tcp_socket.sendall(invalid_request)
        response = _read_http_headers(tcp_socket, timeout_seconds=config.timeout_seconds)
        response_text = response.decode("latin-1", errors="replace")

        if "400" in response_text:
            return True, "14.1.3 invalid upgrade rejected with HTTP 400"
        if "101 Switching Protocols" in response_text:
            return False, "expected invalid upgrade rejection, but got HTTP 101"
        if response_text.strip() == "":
            return True, "14.1.3 invalid upgrade rejected by closing connection"

        return False, f"expected HTTP 400 or close, got: {response_text!r}"
    except Exception as error:
        return False, f"14.1.3 failed: {error}"
    finally:
        if tcp_socket is not None:
            tcp_socket.close()
        stop_broker(process)


def run_14_2_1_full_session_over_websocket(config) -> tuple[bool, str]:
    process = None
    topic_name = _unique_topic("full-session")
    payload = b"ws-full-session"
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as subscriber:
            sub_connack = subscriber.connect(host, ws_port, client_id=_unique_client_id("ws-sub"), clean_start=True)
            assert_connack(sub_connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic_name, qos=0)
            if not suback_codes:
                return False, "expected non-empty SUBACK over WebSocket"
            assert_reason_code(suback_codes[0], 0x00)

            with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as publisher:
                pub_connack = publisher.connect(host, ws_port, client_id=_unique_client_id("ws-pub"), clean_start=True)
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                publish_reason = publisher.publish(topic_name, payload, qos=0)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            message = messages[0]
            if message.topic != topic_name or message.payload != payload:
                return False, "subscriber did not receive expected QoS0 payload over WebSocket"

        return True, "14.2.1 full MQTT session over WebSocket works"
    except Exception as error:
        return False, f"14.2.1 failed: {error}"
    finally:
        stop_broker(process)


def run_14_2_2_qos1_over_websocket(config) -> tuple[bool, str]:
    process = None
    topic_name = _unique_topic("qos1")
    payload = b"ws-qos1"
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as subscriber:
            subscriber.connect(host, ws_port, client_id=_unique_client_id("ws-q1-sub"), clean_start=True)
            suback_codes = subscriber.subscribe(topic_name, qos=1)
            if not suback_codes:
                return False, "expected SUBACK for QoS1 over WebSocket"
            assert_reason_code(suback_codes[0], 0x01)

            with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as publisher:
                publisher.connect(host, ws_port, client_id=_unique_client_id("ws-q1-pub"), clean_start=True)
                publish_reason = publisher.publish(topic_name, payload, qos=1)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            message = messages[0]
            if message.qos != 1 or message.payload != payload:
                return False, "expected exactly one QoS1 message over WebSocket"

        return True, "14.2.2 QoS1 end-to-end over WebSocket works"
    except Exception as error:
        return False, f"14.2.2 failed: {error}"
    finally:
        stop_broker(process)


def run_14_2_3_qos2_over_websocket(config) -> tuple[bool, str]:
    process = None
    topic_name = _unique_topic("qos2")
    payload = b"ws-qos2"
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as subscriber:
            subscriber.connect(host, ws_port, client_id=_unique_client_id("ws-q2-sub"), clean_start=True)
            suback_codes = subscriber.subscribe(topic_name, qos=2)
            if not suback_codes:
                return False, "expected SUBACK for QoS2 over WebSocket"
            assert_reason_code(suback_codes[0], 0x02)

            with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as publisher:
                publisher.connect(host, ws_port, client_id=_unique_client_id("ws-q2-pub"), clean_start=True)
                publish_reason = publisher.publish(topic_name, payload, qos=2)
                assert_reason_code(publish_reason, 0x00)

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            message = messages[0]
            if message.qos != 2 or message.payload != payload:
                return False, "expected exactly one QoS2 message over WebSocket"

        return True, "14.2.3 QoS2 end-to-end over WebSocket works"
    except Exception as error:
        return False, f"14.2.3 failed: {error}"
    finally:
        stop_broker(process)


def run_14_2_4_large_payload_fragmented_frames(config) -> tuple[bool, str]:
    process = None
    raw_socket = None
    topic_name = _unique_topic("fragmented")
    payload = b"X" * 8192
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()

        with MqttClient(timeout_seconds=config.timeout_seconds, transport="websockets") as subscriber:
            subscriber.connect(host, ws_port, client_id=_unique_client_id("ws-frag-sub"), clean_start=True)
            suback_codes = subscriber.subscribe(topic_name, qos=0)
            if not suback_codes:
                return False, "expected SUBACK for fragmented-frame scenario"
            assert_reason_code(suback_codes[0], 0x00)

            raw_socket, handshake_text = _websocket_handshake(
                host,
                ws_port,
                timeout_seconds=config.timeout_seconds,
                include_protocol=True,
            )
            if "101 Switching Protocols" not in handshake_text:
                return False, "raw WebSocket publisher handshake failed"

            connect_payload = encode_utf8_string(_unique_client_id("ws-frag-pub"))
            connect_packet = build_connect_packet(payload=connect_payload)
            raw_socket.sendall(_encode_ws_frame(opcode=0x2, payload=connect_packet, fin=True, masked=True))

            opcode, _fin, connack_payload = _read_ws_frame(raw_socket, timeout_seconds=config.timeout_seconds)
            if opcode != 0x2:
                return False, f"expected binary CONNACK frame, got opcode=0x{opcode:X}"
            connack_reason = _decode_connack_reason_code(connack_payload)
            if connack_reason != 0x00:
                return False, f"expected CONNACK success for raw WS publisher, got 0x{connack_reason:02X}"

            publish_packet = build_publish_packet(topic=topic_name, payload=payload, qos=0)
            split_index = len(publish_packet) // 2
            part_one = publish_packet[:split_index]
            part_two = publish_packet[split_index:]

            raw_socket.sendall(_encode_ws_frame(opcode=0x2, payload=part_one, fin=False, masked=True))
            raw_socket.sendall(_encode_ws_frame(opcode=0x0, payload=part_two, fin=True, masked=True))

            messages = subscriber.collect_messages(count=1, timeout=config.timeout_seconds)
            message = messages[0]
            if message.topic != topic_name:
                return False, "expected fragmented publish on subscribed topic"
            if message.payload != payload:
                return False, "expected full payload reassembled from fragmented frames"

        return True, "14.2.4 large payload over fragmented WebSocket frames delivered"
    except Exception as error:
        return False, f"14.2.4 failed: {error}"
    finally:
        if raw_socket is not None:
            raw_socket.close()
        stop_broker(process)


def run_14_2_5_binary_and_text_frame_handling(config) -> tuple[bool, str]:
    process = None
    raw_socket = None
    try:
        host, _mqtt_port, ws_port, process = _start_isolated_broker()
        raw_socket, handshake_text = _websocket_handshake(
            host,
            ws_port,
            timeout_seconds=config.timeout_seconds,
            include_protocol=True,
        )
        if "101 Switching Protocols" not in handshake_text:
            return False, "WebSocket handshake failed for frame-type test"

        connect_payload = encode_utf8_string(_unique_client_id("ws-text-first"))
        connect_packet = build_connect_packet(payload=connect_payload)

        # Intentionally send CONNECT bytes as TEXT frame.
        # This spec test expects the broker to reject non-binary MQTT framing.
        raw_socket.sendall(_encode_ws_frame(opcode=0x1, payload=connect_packet, fin=True, masked=True))

        try:
            opcode, _fin, payload = _read_ws_frame(raw_socket, timeout_seconds=1.0)
        except TimeoutError:
            return (
                False,
                "14.2.5 expected explicit broker reaction for text MQTT frame "
                "(close or error), but observed no response (intentional red test)",
            )
        except socket.timeout:
            return (
                False,
                "14.2.5 expected explicit broker reaction for text MQTT frame "
                "(close or error), but observed no response (intentional red test)",
            )
        except EOFError:
            return True, "14.2.5 broker closed connection for text MQTT frame"

        if opcode == 0x8:
            return True, "14.2.5 broker returned CLOSE frame for text MQTT frame"
        if opcode == 0x2 and payload and payload[0] == 0x20:
            return (
                False,
                "14.2.5 expected rejection of text MQTT frame, "
                "but broker produced CONNACK (intentional red test)",
            )

        return False, f"14.2.5 expected close/error, got opcode=0x{opcode:X}"
    except Exception as error:
        return False, f"14.2.5 failed: {error}"
    finally:
        if raw_socket is not None:
            raw_socket.close()
        stop_broker(process)


TEST_CASES = [
    {
        "name": "websocket/valid_upgrade_established",
        "description": "14.1.1 HTTP Upgrade with correct headers establishes WebSocket connection",
        "run": run_14_1_1_valid_upgrade_established,
    },
    {
        "name": "websocket/missing_subprotocol_rejected",
        "description": "14.1.2 Missing Sec-WebSocket-Protocol mqtt is rejected",
        "run": run_14_1_2_missing_subprotocol_rejected,
    },
    {
        "name": "websocket/invalid_upgrade_http_400",
        "description": "14.1.3 Invalid upgrade request yields HTTP 400 or connection close",
        "run": run_14_1_3_invalid_upgrade_http_400,
    },
    {
        "name": "websocket/full_session_over_websocket",
        "description": "14.2.1 Full MQTT session over WebSocket works",
        "run": run_14_2_1_full_session_over_websocket,
    },
    {
        "name": "websocket/qos1_over_websocket",
        "description": "14.2.2 QoS1 end-to-end over WebSocket works",
        "run": run_14_2_2_qos1_over_websocket,
    },
    {
        "name": "websocket/qos2_over_websocket",
        "description": "14.2.3 QoS2 end-to-end over WebSocket works",
        "run": run_14_2_3_qos2_over_websocket,
    },
    {
        "name": "websocket/fragmented_frames_large_payload",
        "description": "14.2.4 Large payload over fragmented WebSocket frames is delivered",
        "run": run_14_2_4_large_payload_fragmented_frames,
    },
    {
        "name": "websocket/binary_and_text_frame_handling",
        "description": "14.2.5 Binary and text frame handling is spec-conform",
        "run": run_14_2_5_binary_and_text_frame_handling,
    },
]