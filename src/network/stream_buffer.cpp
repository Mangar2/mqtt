#include "network/stream_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mqtt {

void StreamBuffer::append(std::span<const uint8_t> data) {
  buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::optional<std::size_t> StreamBuffer::front_packet_size() const noexcept {
  // Need at least 2 bytes: type byte + at least 1 RL byte.
  if (buffer_.size() < 2) {
    return std::nullopt;
  }

  // Parse the variable-byte-integer Remaining Length at offset 1.
  std::size_t rl_value = 0;
  std::size_t rl_bytes = 0;
  constexpr uint8_t k_continuation_bit = 0x80U;
  constexpr uint8_t k_value_mask = 0x7FU;
  constexpr std::size_t k_max_rl_bytes = 4;

  for (std::size_t idx = 0; idx < k_max_rl_bytes; ++idx) {
    std::size_t buf_idx = idx + 1; // skip type byte
    if (buf_idx >= buffer_.size()) {
      return std::nullopt; // need more bytes
    }
    uint8_t byte = buffer_[buf_idx];
    rl_value |= (static_cast<std::size_t>(byte & k_value_mask) << (7u * idx));
    ++rl_bytes;
    if ((byte & k_continuation_bit) == 0) {
      break; // last RL byte
    }
  }

  // 1 (type byte) + rl_bytes + rl_value = total packet size
  return 1 + rl_bytes + rl_value;
}

bool StreamBuffer::has_complete_packet() const noexcept {
  auto size = front_packet_size();
  return size.has_value() && buffer_.size() >= *size;
}

std::vector<uint8_t> StreamBuffer::consume_packet() {
  auto size = front_packet_size();
  if (!size.has_value() || buffer_.size() < *size) {
    throw std::logic_error("consume_packet() called without a complete packet");
  }
  std::vector<uint8_t> packet(
      buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(*size));
  buffer_.erase(buffer_.begin(),
                buffer_.begin() + static_cast<std::ptrdiff_t>(*size));
  return packet;
}

bool StreamBuffer::is_empty() const noexcept { return buffer_.empty(); }

} // namespace mqtt
