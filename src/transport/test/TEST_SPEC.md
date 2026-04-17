# transport/test — Unit Test Plan (Module 14.2)

All tests in `websocket_test.cpp`.  Catch2 tag: `[transport]`.

---

## WebSocketHandshake (14.2.1)

| Test case | Behaviour |
|-----------|-----------|
| `handshake_incomplete_on_partial_request` | Partial HTTP bytes → `is_complete()` is false |
| `handshake_complete_on_valid_upgrade` | Full valid HTTP upgrade → `is_complete()` is true |
| `handshake_response_correct_accept_key` | RFC 6455 §1.3 test vector: key "dGhlIHNhbXBsZSBub25jZQ==" → accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" |
| `handshake_response_contains_101` | Response bytes include "101 Switching Protocols" |
| `handshake_rejects_missing_upgrade_header` | No `Upgrade: websocket` → throws `TransportException(InvalidHandshake)` |
| `handshake_rejects_wrong_version` | `Sec-WebSocket-Version: 8` → throws `TransportException(InvalidHandshake)` |
| `handshake_rejects_missing_key` | No `Sec-WebSocket-Key` header → throws |
| `handshake_rejects_missing_connection_header` | No `Connection: Upgrade` header → throws |
| `handshake_build_response_before_complete_throws` | `build_response()` on incomplete state → throws `std::logic_error` |
| `handshake_second_append_noop_after_complete` | Appending more bytes after completion does not change state |

---

## WebSocketFrameCodec — decode (14.2.2 / 14.2.3)

| Test case | Behaviour |
|-----------|-----------|
| `frame_no_frame_on_empty` | No bytes appended → `has_frame()` is false |
| `frame_decode_small_unmasked` | Unmasked binary frame ≤ 125 bytes → correct payload |
| `frame_decode_16bit_length` | Payload 126–65535 bytes with 16-bit extended length → correct payload |
| `frame_decode_64bit_length` | Payload encoded with 127 + 8-byte length field → correct payload |
| `frame_decode_masked` | Masked binary frame (client→server style) → payload correctly unmasked |
| `frame_decode_fragmented_delivery` | Frame bytes delivered in two chunks → complete frame available after second chunk |
| `frame_decode_multiple_frames` | Two frames appended at once → both can be consumed in order |
| `frame_decode_ping_opcode` | Ping frame → `WsOpcode::Ping` and correct payload |
| `frame_decode_close_opcode` | Close frame → `WsOpcode::Close` |
| `frame_decode_fin_flag` | Frame with FIN=0 → `WsFrame::fin` is false |
| `frame_consume_empty_throws` | `consume_frame()` with no buffered frames → throws `std::logic_error` |
| `frame_rejects_rsv_bits` | RSV1 set in byte 0 → throws `TransportException(ProtocolError)` |
| `frame_rejects_unknown_opcode` | Opcode 0x3 → throws `TransportException(InvalidOpcode)` |

---

## WebSocketFrameCodec — encode (14.2.2)

| Test case | Behaviour |
|-----------|-----------|
| `frame_encode_binary_small` | `encode_binary` with ≤ 125 bytes → 1-byte length field, correct bytes |
| `frame_encode_binary_16bit` | `encode_binary` with 126 bytes → 16-bit extended length field |
| `frame_encode_binary_roundtrip` | Encode then decode → recovers original payload |
| `frame_encode_control_ping` | `encode_control(Ping, {})` → FIN=1, opcode=Ping |
| `frame_encode_control_close` | `encode_control(Close, {})` → FIN=1, opcode=Close |
