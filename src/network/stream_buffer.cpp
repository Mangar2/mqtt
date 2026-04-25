#include "network/stream_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mqtt {

namespace {

constexpr uint8_t k_continuation_bit = 0x80U;
constexpr uint8_t k_value_mask = 0x7FU;
constexpr std::size_t k_max_rl_bytes = 4U;

} // namespace

StreamBuffer::Chunk::Chunk(std::size_t chunk_size) : data(chunk_size) {}

StreamBuffer::StreamBuffer(StreamBufferConfig config) : config_(config) {
  if (config_.chunk_size < 16U) {
    throw std::invalid_argument("stream_buffer chunk_size must be at least 16");
  }
  if (config_.max_buffered < 1U) {
    throw std::invalid_argument("stream_buffer max_buffered must be at least 1");
  }
}

StreamBuffer::~StreamBuffer() { clear_all_chunks(); }

StreamBuffer::StreamBuffer(StreamBuffer &&other) noexcept
    : config_(other.config_),
      head_(other.head_),
      tail_(other.tail_),
      free_head_(other.free_head_),
      free_count_(other.free_count_),
      allocated_chunk_count_(other.allocated_chunk_count_),
      size_(other.size_) {
  other.head_ = nullptr;
  other.tail_ = nullptr;
  other.free_head_ = nullptr;
  other.free_count_ = 0U;
  other.allocated_chunk_count_ = 0U;
  other.size_ = 0U;
}

StreamBuffer &StreamBuffer::operator=(StreamBuffer &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  clear_all_chunks();
  config_ = other.config_;
  head_ = other.head_;
  tail_ = other.tail_;
  free_head_ = other.free_head_;
  free_count_ = other.free_count_;
  allocated_chunk_count_ = other.allocated_chunk_count_;
  size_ = other.size_;

  other.head_ = nullptr;
  other.tail_ = nullptr;
  other.free_head_ = nullptr;
  other.free_count_ = 0U;
  other.allocated_chunk_count_ = 0U;
  other.size_ = 0U;
  return *this;
}

StreamBufferAppendResult
StreamBuffer::append(std::span<const uint8_t> data) noexcept {
  if (data.empty()) {
    return StreamBufferAppendResult::kOk;
  }

  if (data.size() > (config_.max_buffered - size_)) {
    return StreamBufferAppendResult::kBufferFull;
  }

  std::size_t src_offset = 0U;
  while (src_offset < data.size()) {
    if (tail_ == nullptr || tail_->write_pos >= config_.chunk_size) {
      Chunk *next_chunk = acquire_chunk();
      if (next_chunk == nullptr) {
        return StreamBufferAppendResult::kBufferFull;
      }
      if (tail_ != nullptr) {
        tail_->next = next_chunk;
      } else {
        head_ = next_chunk;
      }
      tail_ = next_chunk;
    }

    const std::size_t tail_free = config_.chunk_size - tail_->write_pos;
    const std::size_t to_copy = std::min(tail_free, data.size() - src_offset);
    std::memcpy(tail_->data.data() + tail_->write_pos, data.data() + src_offset,
                to_copy);
    tail_->write_pos += to_copy;
    src_offset += to_copy;
    size_ += to_copy;
  }

  return StreamBufferAppendResult::kOk;
}

std::optional<std::size_t> StreamBuffer::front_packet_size() const noexcept {
  if (size_ < 2U) {
    return std::nullopt;
  }

  std::array<uint8_t, 5> prefix{};
  const std::size_t prefix_size = peek_bytes(prefix.data(), prefix.size());
  if (prefix_size < 2U) {
    return std::nullopt;
  }

  std::size_t rl_value = 0;
  std::size_t rl_bytes = 0;

  for (std::size_t idx = 0; idx < k_max_rl_bytes; ++idx) {
    const std::size_t header_index = idx + 1U;
    if (header_index >= prefix_size) {
      return std::nullopt;
    }
    const uint8_t byte = prefix[header_index];
    rl_value |= (static_cast<std::size_t>(byte & k_value_mask) << (7U * idx));
    ++rl_bytes;

    if ((byte & k_continuation_bit) == 0) {
      break;
    }

    if (idx + 1U == k_max_rl_bytes) {
      const std::size_t malformed_frame_size = 1U + k_max_rl_bytes + 1U;
      if (size_ >= malformed_frame_size) {
        return malformed_frame_size;
      }
      return std::nullopt;
    }
  }

  return 1U + rl_bytes + rl_value;
}

bool StreamBuffer::has_complete_packet() const noexcept {
  const auto packet_size = front_packet_size();
  return packet_size.has_value() && size_ >= *packet_size;
}

std::vector<uint8_t> StreamBuffer::consume_packet() {
  const auto packet_size = front_packet_size();
  if (!packet_size.has_value() || size_ < *packet_size) {
    throw std::logic_error("consume_packet() called without a complete packet");
  }

  std::vector<uint8_t> packet;
  packet.reserve(*packet_size);

  std::size_t remaining = *packet_size;
  while (remaining > 0U) {
    if (head_ == nullptr) {
      throw std::logic_error("stream_buffer internal state corrupted");
    }

    const std::size_t available = head_->write_pos - head_->read_pos;
    const std::size_t to_copy = std::min(available, remaining);
    const std::size_t old_size = packet.size();
    packet.resize(old_size + to_copy);
    std::memcpy(packet.data() + old_size, head_->data.data() + head_->read_pos,
                to_copy);

    head_->read_pos += to_copy;
    remaining -= to_copy;
    size_ -= to_copy;

    if (head_->read_pos == head_->write_pos) {
      Chunk *old_head = head_;
      head_ = head_->next;
      if (head_ == nullptr) {
        tail_ = nullptr;
      }
      release_chunk(old_head);
    }
  }

  return packet;
}

bool StreamBuffer::is_empty() const noexcept { return size_ == 0U; }

std::size_t StreamBuffer::size() const noexcept { return size_; }

std::size_t StreamBuffer::capacity() const noexcept {
  return allocated_chunk_count_ * config_.chunk_size;
}

StreamBuffer::Chunk *StreamBuffer::acquire_chunk() {
  if (free_head_ != nullptr) {
    Chunk *chunk = free_head_;
    free_head_ = free_head_->next;
    --free_count_;
    chunk->next = nullptr;
    chunk->read_pos = 0U;
    chunk->write_pos = 0U;
    return chunk;
  }

  try {
    Chunk *chunk = new Chunk(config_.chunk_size);
    ++allocated_chunk_count_;
    return chunk;
  } catch (...) {
    return nullptr;
  }
}

void StreamBuffer::release_chunk(Chunk *chunk) noexcept {
  if (chunk == nullptr) {
    return;
  }

  chunk->next = nullptr;
  chunk->read_pos = 0U;
  chunk->write_pos = 0U;

  if (free_count_ < config_.free_list_max) {
    chunk->next = free_head_;
    free_head_ = chunk;
    ++free_count_;
    return;
  }

  delete chunk;
  --allocated_chunk_count_;
}

void StreamBuffer::clear_all_chunks() noexcept {
  Chunk *iter = head_;
  while (iter != nullptr) {
    Chunk *next = iter->next;
    delete iter;
    iter = next;
  }

  iter = free_head_;
  while (iter != nullptr) {
    Chunk *next = iter->next;
    delete iter;
    iter = next;
  }

  head_ = nullptr;
  tail_ = nullptr;
  free_head_ = nullptr;
  free_count_ = 0U;
  allocated_chunk_count_ = 0U;
  size_ = 0U;
}

std::size_t StreamBuffer::peek_bytes(uint8_t *out,
                                     std::size_t max_count) const noexcept {
  if (out == nullptr || max_count == 0U || head_ == nullptr) {
    return 0U;
  }

  std::size_t copied = 0U;
  const Chunk *chunk = head_;
  while (chunk != nullptr && copied < max_count) {
    const std::size_t available = chunk->write_pos - chunk->read_pos;
    const std::size_t take = std::min(max_count - copied, available);
    if (take > 0U) {
      std::memcpy(out + copied, chunk->data.data() + chunk->read_pos, take);
      copied += take;
    }
    chunk = chunk->next;
  }

  return copied;
}

} // namespace mqtt
