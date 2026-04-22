#pragma once

/**
 * @file websocket_transport.h
 * @brief WebSocketTransport — composed WS transport over TcpConnection
 * (Module 14.2.4).
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "network/tcp_connection.h"
#include "transport/websocket_frame_codec.h"

namespace mqtt {

/**
 * @brief Result of one WebSocketTransport::read_chunk() call.
 */
struct WsReadChunk {
  std::vector<uint8_t>
      data; ///< Decoded MQTT bytes (may be empty on timeout or control frame).
  bool timed_out{false}; ///< True when the TCP recv timed out with no data.
  bool eof{false};       ///< True on WS Close frame or TCP EOF.
};

/**
 * @brief Composed WebSocket transport over a TcpConnection (Module 14.2.4).
 *
 * Performs the RFC 6455 HTTP upgrade handshake on construction, then wraps
 * the underlying `TcpConnection` so that callers read and write raw MQTT bytes
 * without dealing with WS framing at all.
 *
 * ### Read path
 * `read_chunk()` reads one TCP recv buffer, feeds raw bytes into
 * `WebSocketFrameCodec`, collects all complete Binary-frame payloads, and
 * returns them.  Ping frames are answered with Pong automatically.  A Close
 * frame or TCP EOF sets `WsReadChunk::eof`.
 *
 * ### Write path
 * `write_frame()` wraps MQTT bytes in a WS Binary frame and writes
 * synchronously.  `encode_frame()` (static) pre-frames bytes for insertion
 * into a `WriteQueue` sink that writes to the raw `TcpConnection`.
 *
 * ### Queue sink compatibility
 * Configure `WriteQueue::set_sink(...)` with a writer bound to `tcp()` and
 * call `encode_frame()` before `WriteQueue::enqueue()`.
 *
 * Thread safety: none — external synchronisation required.
 */
class WebSocketTransport {
public:
  /**
   * @brief Perform the HTTP upgrade handshake on @p conn.
   *
   * Reads the HTTP request from the connection and sends the 101 response.
   * Ownership of the connection is NOT transferred; the caller must keep it
   * alive for the lifetime of this object.
   *
   * @throws TransportException(InvalidHandshake) on any protocol failure.
   */
  explicit WebSocketTransport(TcpConnection &conn);

  WebSocketTransport(const WebSocketTransport &) = delete;
  WebSocketTransport &operator=(const WebSocketTransport &) = delete;

  /**
   * @brief Read one TCP chunk, decode WS frames, and return MQTT bytes.
   *
   * Ping frames are answered automatically (sync Pong write).
   *
   * @return WsReadChunk with:
   *   - `data` non-empty — decoded MQTT bytes from Binary frames.
   *   - `timed_out = true` — recv timeout; no MQTT data this call.
   *   - `eof = true` — WS Close frame received or TCP connection closed.
   */
  [[nodiscard]] WsReadChunk read_chunk();

  /**
   * @brief Wrap @p mqtt_bytes in a WS Binary frame and write synchronously.
   *
   * Used for in-band writes before or outside the async drain thread.
   *
   * @param mqtt_bytes Raw MQTT packet bytes.
   * @return `true` on success; `false` on write error.
   */
  [[nodiscard]] bool write_frame(std::span<const uint8_t> mqtt_bytes) noexcept;

  /**
   * @brief Wrap @p mqtt_bytes in a WS Binary frame (for WriteQueue
   * pre-framing).
   *
  * The returned bytes can be passed directly to `WriteQueue::enqueue()`.
  * A configured queue sink can write them to the raw `TcpConnection` via
  * `tcp()`.
   *
   * @param mqtt_bytes Raw MQTT packet bytes.
   * @return Encoded WS Binary frame bytes.
   */
  [[nodiscard]] static std::vector<uint8_t>
  encode_frame(std::span<const uint8_t> mqtt_bytes);

  /**
   * @brief Set the socket receive timeout (delegates to the TcpConnection).
   * @param milliseconds_val Timeout in milliseconds; 0 = block indefinitely.
   */
  void set_receive_timeout(uint32_t milliseconds_val) noexcept;

  /**
  * @brief Return the underlying TcpConnection used by queue sink writers.
   */
  [[nodiscard]] TcpConnection &tcp() noexcept;

private:
  static constexpr std::size_t k_read_chunk_size = 4096U;

  TcpConnection &conn_;
  WebSocketFrameCodec codec_;
  bool fragmented_message_active_{false};
  std::vector<uint8_t> fragmented_payload_;
};

} // namespace mqtt
