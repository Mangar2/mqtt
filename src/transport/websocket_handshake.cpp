#include "transport/websocket_handshake.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "transport/transport_error.h"

namespace mqtt {

//
// Internal helpers (SHA-1 + Base64)

namespace {

/// GUID appended to the client key per RFC 6455 §4.2.2.
constexpr std::string_view k_ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

//  SHA-1

constexpr std::uint32_t sha1_rot_left(std::uint32_t val,
                                      std::uint32_t bits) noexcept {
  return (val << bits) | (val >> (32U - bits));
}

std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> data) {
  // Initial hash values (FIPS 180-4 §6.1).
  std::array<std::uint32_t, 5> hash = {0x67452301U, 0xEFCDAB89U, 0x98BADCFEU,
                                       0x10325476U, 0xC3D2E1F0U};

  // Pre-processing: append 0x80, zero-pad to 56 mod 64, append 64-bit
  // big-endian bit count.
  std::vector<std::uint8_t> msg(data.begin(), data.end());
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8U;
  msg.push_back(0x80U);
  while (msg.size() % 64U != 56U) {
    msg.push_back(0x00U);
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    msg.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xFFU));
  }

  // Process each 512-bit (64-byte) block.
  const std::size_t num_blocks = msg.size() / 64U;
  for (std::size_t blk = 0; blk < num_blocks; ++blk) {
    std::array<std::uint32_t, 80> wrd{};
    for (std::uint32_t idx = 0U; idx < 16U; ++idx) {
      const std::size_t off = (blk * 64U) + (idx * 4U);
      wrd[idx] = (static_cast<std::uint32_t>(msg[off]) << 24U) |
                 (static_cast<std::uint32_t>(msg[off + 1U]) << 16U) |
                 (static_cast<std::uint32_t>(msg[off + 2U]) << 8U) |
                 static_cast<std::uint32_t>(msg[off + 3U]);
    }
    for (std::uint32_t idx = 16U; idx < 80U; ++idx) {
      wrd[idx] = sha1_rot_left(
          wrd[idx - 3U] ^ wrd[idx - 8U] ^ wrd[idx - 14U] ^ wrd[idx - 16U], 1U);
    }

    std::uint32_t reg_a = hash[0];
    std::uint32_t reg_b = hash[1];
    std::uint32_t reg_c = hash[2];
    std::uint32_t reg_d = hash[3];
    std::uint32_t reg_e = hash[4];

    for (std::uint32_t idx = 0U; idx < 80U; ++idx) {
      std::uint32_t fun = 0U;
      std::uint32_t con = 0U;
      if (idx < 20U) {
        fun = (reg_b & reg_c) | (~reg_b & reg_d);
        con = 0x5A827999U;
      } else if (idx < 40U) {
        fun = reg_b ^ reg_c ^ reg_d;
        con = 0x6ED9EBA1U;
      } else if (idx < 60U) {
        fun = (reg_b & reg_c) | (reg_b & reg_d) | (reg_c & reg_d);
        con = 0x8F1BBCDCU;
      } else {
        fun = reg_b ^ reg_c ^ reg_d;
        con = 0xCA62C1D6U;
      }
      const std::uint32_t tmp =
          sha1_rot_left(reg_a, 5U) + fun + reg_e + con + wrd[idx];
      reg_e = reg_d;
      reg_d = reg_c;
      reg_c = sha1_rot_left(reg_b, 30U);
      reg_b = reg_a;
      reg_a = tmp;
    }

    hash[0] += reg_a;
    hash[1] += reg_b;
    hash[2] += reg_c;
    hash[3] += reg_d;
    hash[4] += reg_e;
  }

  std::array<std::uint8_t, 20> digest{};
  for (std::uint32_t idx = 0U; idx < 5U; ++idx) {
    digest[(idx * 4U)] = static_cast<std::uint8_t>((hash[idx] >> 24U) & 0xFFU);
    digest[(idx * 4U) + 1U] =
        static_cast<std::uint8_t>((hash[idx] >> 16U) & 0xFFU);
    digest[(idx * 4U) + 2U] =
        static_cast<std::uint8_t>((hash[idx] >> 8U) & 0xFFU);
    digest[(idx * 4U) + 3U] = static_cast<std::uint8_t>(hash[idx] & 0xFFU);
  }
  return digest;
}

//  Base64

constexpr std::string_view k_b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(std::span<const std::uint8_t> data) {
  std::string out;
  out.reserve(((data.size() + 2U) / 3U) * 4U);

  std::uint32_t acc = 0U;
  int bits = -6;
  for (const std::uint8_t byt : data) {
    acc = (acc << 8U) | byt;
    bits += 8;
    while (bits >= 0) {
      out.push_back(k_b64_chars[(acc >> bits) & 0x3FU]);
      bits -= 6;
    }
  }
  if (bits > -6) {
    out.push_back(k_b64_chars[((acc << 8U) >> (bits + 8)) & 0x3FU]);
  }
  while (out.size() % 4U != 0U) {
    out.push_back('=');
  }
  return out;
}

//  Header parsing helpers

/// Case-insensitive ASCII comparison of two string_views.
bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t idx = 0U; idx < lhs.size(); ++idx) {
    if (std::tolower(static_cast<unsigned char>(lhs[idx])) !=
        std::tolower(static_cast<unsigned char>(rhs[idx]))) {
      return false;
    }
  }
  return true;
}

/// Trim leading and trailing ASCII whitespace from a string_view.
std::string_view trim(std::string_view str) noexcept {
  while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
    str.remove_prefix(1U);
  }
  while (!str.empty() && (str.back() == ' ' || str.back() == '\t' ||
                          str.back() == '\r' || str.back() == '\n')) {
    str.remove_suffix(1U);
  }
  return str;
}

/// Case-insensitive search for a token in a comma-separated header value.
bool header_value_contains(std::string_view header_val,
                           std::string_view token) noexcept {
  std::size_t pos = 0U;
  while (pos < header_val.size()) {
    const std::size_t comma = header_val.find(',', pos);
    const std::string_view part = trim(header_val.substr(
        pos, comma == std::string_view::npos ? std::string_view::npos
                                             : comma - pos));
    if (iequals(part, token)) {
      return true;
    }
    if (comma == std::string_view::npos) {
      break;
    }
    pos = comma + 1U;
  }
  return false;
}

/// Compute Sec-WebSocket-Accept from a Sec-WebSocket-Key value.
std::string compute_accept_key(std::string_view ws_key) {
  const std::string concatenated = std::string(ws_key) + std::string(k_ws_guid);
  const auto raw_bytes = std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(concatenated.data()),
      concatenated.size());
  const auto digest = sha1(raw_bytes);
  return base64_encode(std::span<const std::uint8_t>(digest));
}

} // namespace

//
// WebSocketHandshake implementation

void WebSocketHandshake::append(std::span<const uint8_t> data) {
  if (complete_) {
    return;
  }
  raw_.append(reinterpret_cast<const char *>(data.data()), data.size());
  parse_();
}

bool WebSocketHandshake::is_complete() const noexcept { return complete_; }

std::vector<uint8_t> WebSocketHandshake::build_response() const {
  if (!complete_) {
    throw std::logic_error("WebSocketHandshake::build_response called before "
                           "handshake is complete");
  }
  const std::string accept_key = compute_accept_key(ws_key_);
  std::ostringstream oss;
  oss << "HTTP/1.1 101 Switching Protocols\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
      << "\r\n";
  const std::string resp = oss.str();
  return std::vector<uint8_t>(resp.begin(), resp.end());
}

void WebSocketHandshake::parse_() {
  // Wait until the full header block has arrived.
  const std::size_t end_pos = raw_.find("\r\n\r\n");
  if (end_pos == std::string::npos) {
    return; // Incomplete — wait for more data.
  }
  const std::string_view headers(raw_.data(), end_pos + 4U);

  bool found_upgrade = false;
  bool found_connection = false;
  bool found_version = false;
  bool found_subprotocol = false;

  // Parse header lines (skip the request line).
  std::size_t line_start = headers.find("\r\n");
  if (line_start == std::string_view::npos) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Malformed HTTP request"};
  }
  line_start += 2U;

  while (line_start < headers.size()) {
    const std::size_t line_end = headers.find("\r\n", line_start);
    if (line_end == std::string_view::npos) {
      break;
    }
    const std::string_view line =
        headers.substr(line_start, line_end - line_start);
    line_start = line_end + 2U;

    if (line.empty()) {
      break;
    }

    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    const std::string_view name = trim(line.substr(0U, colon));
    const std::string_view value = trim(line.substr(colon + 1U));

    if (iequals(name, "Upgrade") && iequals(value, "websocket")) {
      found_upgrade = true;
    } else if (iequals(name, "Connection") &&
               header_value_contains(value, "Upgrade")) {
      found_connection = true;
    } else if (iequals(name, "Sec-WebSocket-Key")) {
      ws_key_ = std::string(value);
    } else if (iequals(name, "Sec-WebSocket-Version")) {
      if (value != "13") {
        throw TransportException{
            TransportError::InvalidHandshake,
            "Unsupported Sec-WebSocket-Version (expected 13)"};
      }
      found_version = true;
    } else if (iequals(name, "Sec-WebSocket-Protocol")) {
      found_subprotocol = header_value_contains(value, "mqtt");
    }
  }

  if (!found_upgrade) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Missing or invalid Upgrade: websocket header"};
  }
  if (!found_connection) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Missing or invalid Connection: Upgrade header"};
  }
  if (ws_key_.empty()) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Missing Sec-WebSocket-Key header"};
  }
  if (!found_version) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Missing Sec-WebSocket-Version header"};
  }
  if (!found_subprotocol) {
    throw TransportException{TransportError::InvalidHandshake,
                             "Missing Sec-WebSocket-Protocol: mqtt header"};
  }

  complete_ = true;
}

} // namespace mqtt
