# transport — Transport Extensions (Module 14)

Alternative transport layer beyond plain TCP.  Sits between the Network Layer
(Module 6) and the Connection Handler (Module 7).  Depends on: Module 6.

> **Note:** Module 14.1 (TLS Transport) is **not implemented**.
> The broker accepts plain TCP (MQTT) and WebSocket (WS) connections only.
> TLS termination can be achieved with an external reverse proxy (e.g. nginx,
> HAProxy, stunnel) in front of the broker, which is the recommended deployment
> pattern for production environments.

---

## Purpose

Provides a WebSocket upgrade handshake and frame codec so that MQTT clients
that connect via `ws://` (MQTT over WebSocket, RFC 6455) can be served by the
same broker code without modification to the MQTT codec or connection handler.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `transport_error.h` | 14 | `TransportError` enum and `TransportException` |
| `websocket_handshake.h/.cpp` | 14.2.1 | `WebSocketHandshake` — parses HTTP upgrade request; produces 101 response |
| `websocket_frame_codec.h/.cpp` | 14.2.2–3 | `WsOpcode`, `WsFrame`, `WebSocketFrameCodec` — frame decoder (buffered), frame encoder |
| `websocket_transport.h/.cpp` | 14.2.4 | `WebSocketTransport` — composed transport: performs WS handshake on construction, wraps `TcpConnection` reads/writes with transparent WS framing |

---

## Component Details

### `TransportError` / `TransportException`

| Code | Meaning |
|------|---------|
| `InvalidHandshake` | HTTP upgrade request is malformed or missing required headers |
| `ProtocolError` | WebSocket protocol violation (e.g. RSV bits set) |
| `FrameTooLarge` | WebSocket payload exceeds 128 MiB |
| `InvalidOpcode` | Unknown WebSocket opcode |

---

### `WebSocketHandshake` (14.2.1)

Parses a raw HTTP/1.1 upgrade request and generates the HTTP 101 response.

**Validated headers (RFC 6455 §4.2.1):**
- `Upgrade: websocket`
- `Connection: Upgrade`
- `Sec-WebSocket-Key` (non-empty)
- `Sec-WebSocket-Version: 13`

**Response** includes `Sec-WebSocket-Accept` computed as
`base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`.

SHA-1 and Base64 are implemented without external libraries as anonymous-namespace
helpers inside `websocket_handshake.cpp`.

**Public API:**

| Method | Description |
|--------|-------------|
| `append(span<uint8_t>)` | Feed raw TCP bytes; parses when `\r\n\r\n` is seen |
| `is_complete()` → `bool` | `true` once a valid request has been fully parsed |
| `build_response()` → `vector<uint8_t>` | HTTP 101 response bytes; throws if not complete |

---

### `WebSocketFrameCodec` (14.2.2 + 14.2.3)

Buffers raw bytes from the TCP stream, decodes inbound WebSocket frames, and
provides static helpers to encode outbound frames for the server side.

**Decode flow:**
1. `append(bytes)` — feed raw bytes into the internal buffer.
2. `has_frame()` — returns `true` when at least one complete frame is decoded.
3. `consume_frame()` — removes and returns the oldest decoded `WsFrame`.
4. `WsFrame::payload` when `opcode == Binary` contains the MQTT packet bytes (14.2.3).

**Encode flow:**
- `encode_binary(payload)` — wraps MQTT bytes in a FIN=1, Binary frame (no mask).
- `encode_control(opcode, payload)` — encodes Close / Ping / Pong frames.

**Length encoding (RFC 6455 §5.2):**

| Payload size | Encoding |
|---|---|
| 0 – 125 bytes | 1-byte length |
| 126 – 65 535 bytes | `126` + 2-byte big-endian length |
| 65 536 – 128 MiB | `127` + 8-byte big-endian length |

Client-to-server frames are always masked; the codec unmasks transparently.
Server-to-client frames are never masked.

---

### `WebSocketTransport` (14.2.4)

Composed transport that performs the RFC 6455 handshake on construction and
wraps a `TcpConnection` so that callers read and write raw MQTT bytes without
needing to know about the WebSocket framing layer.

**Constructor:** `WebSocketTransport(TcpConnection& conn)`
- Reads the HTTP upgrade request from `conn` and writes the 101 response.
- Throws `TransportException(InvalidHandshake)` on protocol failure.

**`read_chunk()`** → `WsReadChunk`
- Reads one TCP recv chunk, feeds raw bytes into `WebSocketFrameCodec`.
- Collects all complete Binary-frame payloads into `WsReadChunk::data`.
- Auto-responds to Ping frames with a Pong (sync write).
- Returns `eof = true` on a Close frame or TCP EOF.
- Returns `timed_out = true` on a recv timeout.

**`write_frame(span<const uint8_t>)`** → `bool`
- Wraps MQTT bytes in a WS Binary frame and writes synchronously to TCP.
- Used for in-band writes before the async drain thread starts.

**`encode_frame(span<const uint8_t>)`** → `vector<uint8_t>` *(static)*
- Wraps MQTT bytes in a WS Binary frame for pre-framing into outbound buffers.

**`set_receive_timeout(uint32_t ms)`** — delegates to the underlying `TcpConnection`.

**`tcp()`** → `TcpConnection&` — returns the underlying connection for
low-level writes when needed.

```cpp
// Example usage in connection handler
WebSocketTransport ws(conn);
ws.set_receive_timeout(500U);
WsReadChunk chunk = ws.read_chunk();
if (!chunk.eof && !chunk.timed_out) {
    stream_buf.append(chunk.data);
}
```
