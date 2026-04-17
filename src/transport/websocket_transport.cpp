/**
 * @file websocket_transport.cpp
 * @brief WebSocketTransport implementation (Module 14.2.4).
 */

#include "transport/websocket_transport.h"

#include <array>
#include <cstddef>

#include "transport/transport_error.h"
#include "transport/websocket_handshake.h"

namespace mqtt {

WebSocketTransport::WebSocketTransport(TcpConnection &conn) : conn_(conn) {
  WebSocketHandshake handshake;
  std::array<uint8_t, k_read_chunk_size> raw{};

  while (!handshake.is_complete()) {
    std::ptrdiff_t num = conn_.read(raw);
    if (num <= 0) {
      throw TransportException(TransportError::InvalidHandshake,
                               "websocket handshake failed: socket closed");
    }
    handshake.append(std::span{raw}.first(static_cast<std::size_t>(num)));
  }

  auto response = handshake.build_response();
  (void)conn_.write(response);
}

WsReadChunk WebSocketTransport::read_chunk() {
  std::array<uint8_t, k_read_chunk_size> raw{};
  std::ptrdiff_t num = conn_.read(raw);

  if (num <= 0) {
    return {
        .data = {},
        .timed_out = conn_.last_read_timed_out(),
        .eof = (num == 0),
    };
  }

  try {
    codec_.append(std::span{raw}.first(static_cast<std::size_t>(num)));
  } catch (...) {
    // Any WS protocol violation ends the connection.
    return {.data = {}, .timed_out = false, .eof = true};
  }

  WsReadChunk chunk;
  while (codec_.has_frame()) {
    WsFrame frame = codec_.consume_frame();
    if (frame.opcode == WsOpcode::Binary) {
      chunk.data.insert(chunk.data.end(), frame.payload.begin(),
                        frame.payload.end());
    } else if (frame.opcode == WsOpcode::Close) {
      chunk.eof = true;
      return chunk;
    } else if (frame.opcode == WsOpcode::Ping) {
      auto pong = WebSocketFrameCodec::encode_control(
          WsOpcode::Pong, std::span<const uint8_t>{frame.payload});
      (void)conn_.write(pong);
    }
    // Pong, Continuation, Text: silently ignored.
  }

  return chunk;
}

bool WebSocketTransport::write_frame(
    std::span<const uint8_t> mqtt_bytes) noexcept {
  try {
    auto framed = WebSocketFrameCodec::encode_binary(mqtt_bytes);
    return conn_.write(framed);
  } catch (...) {
    return false;
  }
}

std::vector<uint8_t>
WebSocketTransport::encode_frame(std::span<const uint8_t> mqtt_bytes) {
  return WebSocketFrameCodec::encode_binary(mqtt_bytes);
}

void WebSocketTransport::set_receive_timeout(
    uint32_t milliseconds_val) noexcept {
  conn_.set_receive_timeout(milliseconds_val);
}

TcpConnection &WebSocketTransport::tcp() noexcept { return conn_; }

} // namespace mqtt
