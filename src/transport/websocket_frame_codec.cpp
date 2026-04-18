#include "transport/websocket_frame_codec.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "transport/transport_error.h"

namespace mqtt {

//
// Opcode validation helper

namespace {

bool is_known_opcode(std::uint8_t raw) noexcept {
  return raw == 0x0U || raw == 0x1U || raw == 0x2U || raw == 0x8U ||
         raw == 0x9U || raw == 0xAU;
}

struct FrameHeader {
  bool fin;
  WsOpcode opcode;
  bool masked;
  std::size_t header_size;
  std::size_t payload_len;
};

/// Try to parse the header fields from the front of `buf`.
/// Returns false (not enough data) or throws on protocol violations.
/// On success fills `out` and returns true.
bool parse_header(const std::vector<uint8_t> &buf, FrameHeader &out) {
  if (buf.size() < 2U) {
    return false;
  }

  const std::uint8_t byte0 = buf[0];
  const std::uint8_t byte1 = buf[1];
  const auto rsv = static_cast<std::uint8_t>((byte0 >> 4U) & 0x07U);
  const auto opcode = static_cast<std::uint8_t>(byte0 & 0x0FU);

  if (rsv != 0U) {
    throw TransportException{TransportError::ProtocolError,
                             "WebSocket RSV bits must be zero"};
  }
  if (!is_known_opcode(opcode)) {
    throw TransportException{TransportError::InvalidOpcode,
                             "Unknown WebSocket opcode"};
  }

  const auto len7 = static_cast<std::uint8_t>(byte1 & 0x7FU);
  std::size_t header = 2U;
  std::uint64_t plen = len7;

  if (len7 == 126U) {
    if (buf.size() < 4U) {
      return false;
    }
    plen = (static_cast<std::uint64_t>(buf[2]) << 8U) | buf[3];
    header = 4U;
  } else if (len7 == 127U) {
    if (buf.size() < 10U) {
      return false;
    }
    plen = 0U;
    for (std::size_t idx = 0U; idx < 8U; ++idx) {
      plen = (plen << 8U) | static_cast<std::uint64_t>(buf[2U + idx]);
    }
    header = 10U;
  }

  if (plen > WebSocketFrameCodec::k_max_payload) {
    throw TransportException{TransportError::FrameTooLarge,
                             "WebSocket frame payload exceeds maximum"};
  }
  if (plen >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw TransportException{
        TransportError::FrameTooLarge,
        "WebSocket frame payload too large for this platform"};
  }

  out = {
      .fin = (byte0 & 0x80U) != 0U,
      .opcode = static_cast<WsOpcode>(opcode),
      .masked = (byte1 & 0x80U) != 0U,
      .header_size = header,
      .payload_len = static_cast<std::size_t>(plen),
  };
  return true;
}

} // namespace

//
// Decode pipeline

void WebSocketFrameCodec::append(std::span<const uint8_t> data) {
  buf_.insert(buf_.end(), data.begin(), data.end());
  try_decode_();
}

bool WebSocketFrameCodec::has_frame() const noexcept {
  return !frames_.empty();
}

WsFrame WebSocketFrameCodec::consume_frame() {
  if (frames_.empty()) {
    throw std::logic_error(
        "WebSocketFrameCodec::consume_frame called with no frames available");
  }
  WsFrame frm = std::move(frames_.front());
  frames_.erase(frames_.begin());
  return frm;
}

void WebSocketFrameCodec::try_decode_() {
  while (true) {
    FrameHeader hdr{};
    if (!parse_header(buf_, hdr)) {
      break;
    }

    const std::size_t mask_size = hdr.masked ? 4U : 0U;
    const std::size_t total_size =
        hdr.header_size + mask_size + hdr.payload_len;

    if (buf_.size() < total_size) {
      break;
    }

    WsFrame frm;
    frm.fin = hdr.fin;
    frm.opcode = hdr.opcode;
    frm.payload.resize(hdr.payload_len);

    if (hdr.masked) {
      const std::array<std::uint8_t, 4> mask_key = {
          buf_[hdr.header_size], buf_[hdr.header_size + 1U],
          buf_[hdr.header_size + 2U], buf_[hdr.header_size + 3U]};
      for (std::size_t idx = 0U; idx < hdr.payload_len; ++idx) {
        frm.payload[idx] =
            buf_[hdr.header_size + 4U + idx] ^ mask_key[idx % 4U];
      }
    } else {
      const auto src_begin =
          buf_.begin() + static_cast<std::ptrdiff_t>(hdr.header_size);
      std::copy(src_begin,
                src_begin + static_cast<std::ptrdiff_t>(hdr.payload_len),
                frm.payload.begin());
    }

    frames_.push_back(std::move(frm));
    buf_.erase(buf_.begin(),
               buf_.begin() + static_cast<std::ptrdiff_t>(total_size));
  }
}

//
// Encode helpers

std::vector<uint8_t>
WebSocketFrameCodec::encode_frame_(bool fin, WsOpcode opcode,
                                   std::span<const uint8_t> payload) {
  std::vector<uint8_t> frame;
  const std::size_t plen = payload.size();

  // Byte 0: FIN + opcode (RSV bits stay 0, no mask on server side).
  const std::uint8_t byte0 = static_cast<std::uint8_t>(
      (fin ? 0x80U : 0x00U) | static_cast<std::uint8_t>(opcode));
  frame.push_back(byte0);

  // Byte 1 (and optional extended length): MASK bit is never set for server
  // frames.
  if (plen <= 125U) {
    frame.push_back(static_cast<std::uint8_t>(plen));
  } else if (plen <= 65535U) {
    frame.push_back(126U);
    frame.push_back(static_cast<std::uint8_t>((plen >> 8U) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>(plen & 0xFFU));
  } else {
    frame.push_back(127U);
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<std::uint8_t>((plen >> shift) & 0xFFU));
    }
  }

  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

std::vector<uint8_t>
WebSocketFrameCodec::encode_binary(std::span<const uint8_t> payload) {
  return encode_frame_(true, WsOpcode::Binary, payload);
}

std::vector<uint8_t>
WebSocketFrameCodec::encode_control(WsOpcode opcode,
                                    std::span<const uint8_t> payload) {
  return encode_frame_(true, opcode, payload);
}

} // namespace mqtt
