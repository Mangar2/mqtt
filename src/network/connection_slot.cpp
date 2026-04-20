#include "network/connection_slot.h"

#include <algorithm>
#include <cstddef>

namespace mqtt {

ConnectionSlot::ConnectionSlot(SocketHandle socket_handle,
                               std::size_t read_capacity_bytes,
                               std::size_t write_capacity_bytes)
    : socket_handle_(socket_handle), read_storage_(read_capacity_bytes),
      write_storage_(write_capacity_bytes) {}

ConnectionSlot::ConnectionSlot(ConnectionSlot &&other) noexcept
    : socket_handle_(other.socket_handle_), phase_(other.phase_),
      read_storage_(std::move(other.read_storage_)),
      read_head_index_(other.read_head_index_),
      read_used_size_(other.read_used_size_),
      write_storage_(std::move(other.write_storage_)),
      write_head_index_(other.write_head_index_),
      write_used_size_(other.write_used_size_) {
  other.socket_handle_ = k_invalid_socket;
  other.phase_ = ConnectionPhase::Closing;
  other.read_head_index_ = 0;
  other.read_used_size_ = 0;
  other.write_head_index_ = 0;
  other.write_used_size_ = 0;
}

ConnectionSlot &ConnectionSlot::operator=(ConnectionSlot &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  socket_handle_ = other.socket_handle_;
  phase_ = other.phase_;
  read_storage_ = std::move(other.read_storage_);
  read_head_index_ = other.read_head_index_;
  read_used_size_ = other.read_used_size_;
  write_storage_ = std::move(other.write_storage_);
  write_head_index_ = other.write_head_index_;
  write_used_size_ = other.write_used_size_;

  other.socket_handle_ = k_invalid_socket;
  other.phase_ = ConnectionPhase::Closing;
  other.read_head_index_ = 0;
  other.read_used_size_ = 0;
  other.write_head_index_ = 0;
  other.write_used_size_ = 0;
  return *this;
}

SocketHandle ConnectionSlot::fd() const noexcept { return socket_handle_; }

ConnectionPhase ConnectionSlot::phase() const noexcept { return phase_; }

bool ConnectionSlot::transition_to(ConnectionPhase next_phase) noexcept {
  if (next_phase == phase_) {
    return true;
  }

  switch (phase_) {
  case ConnectionPhase::Connecting:
    if (next_phase == ConnectionPhase::Connected ||
        next_phase == ConnectionPhase::Closing) {
      phase_ = next_phase;
      return true;
    }
    return false;
  case ConnectionPhase::Connected:
    if (next_phase == ConnectionPhase::Closing) {
      phase_ = next_phase;
      return true;
    }
    return false;
  case ConnectionPhase::Closing:
    return false;
  }
  return false;
}

std::size_t ConnectionSlot::read_size() const noexcept { return read_used_size_; }

std::size_t ConnectionSlot::read_capacity() const noexcept {
  return read_storage_.size();
}

std::size_t ConnectionSlot::read_free_space() const noexcept {
  return read_storage_.size() - read_used_size_;
}

bool ConnectionSlot::push_read_bytes(std::span<const uint8_t> data) noexcept {
  return push_bytes(read_storage_, read_head_index_, read_used_size_, data);
}

std::size_t ConnectionSlot::pop_read_bytes(std::size_t bytes_to_pop) noexcept {
  return pop_bytes(read_head_index_, read_used_size_, bytes_to_pop,
                   read_storage_.size());
}

std::span<const uint8_t> ConnectionSlot::read_contiguous_bytes() const noexcept {
  return contiguous_bytes(read_storage_, read_head_index_, read_used_size_);
}

std::size_t ConnectionSlot::write_size() const noexcept {
  return write_used_size_;
}

std::size_t ConnectionSlot::write_capacity() const noexcept {
  return write_storage_.size();
}

std::size_t ConnectionSlot::write_free_space() const noexcept {
  return write_storage_.size() - write_used_size_;
}

bool ConnectionSlot::push_write_bytes(std::span<const uint8_t> data) noexcept {
  return push_bytes(write_storage_, write_head_index_, write_used_size_, data);
}

std::size_t ConnectionSlot::pop_write_bytes(std::size_t bytes_to_pop) noexcept {
  return pop_bytes(write_head_index_, write_used_size_, bytes_to_pop,
                   write_storage_.size());
}

std::span<const uint8_t>
ConnectionSlot::write_contiguous_bytes() const noexcept {
  return contiguous_bytes(write_storage_, write_head_index_, write_used_size_);
}

bool ConnectionSlot::push_bytes(std::vector<uint8_t> &storage,
                                std::size_t &head_index, std::size_t &used_size,
                                std::span<const uint8_t> data) noexcept {
  if (storage.empty() || data.size() > storage.size() - used_size) {
    return false;
  }

  const std::size_t tail_index = (head_index + used_size) % storage.size();
  const std::size_t first_chunk_size =
      std::min(data.size(), storage.size() - tail_index);
  const std::size_t second_chunk_size = data.size() - first_chunk_size;

  std::copy_n(data.begin(), static_cast<std::ptrdiff_t>(first_chunk_size),
              storage.begin() + static_cast<std::ptrdiff_t>(tail_index));
  if (second_chunk_size > 0) {
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(first_chunk_size),
                static_cast<std::ptrdiff_t>(second_chunk_size), storage.begin());
  }
  used_size += data.size();
  return true;
}

std::size_t ConnectionSlot::pop_bytes(std::size_t &head_index,
                                      std::size_t &used_size,
                                      std::size_t bytes_to_pop,
                                      std::size_t capacity) noexcept {
  const std::size_t removed_bytes = std::min(bytes_to_pop, used_size);
  used_size -= removed_bytes;

  if (capacity > 0) {
    head_index = (head_index + removed_bytes) % capacity;
  } else {
    head_index = 0;
  }
  return removed_bytes;
}

std::span<const uint8_t>
ConnectionSlot::contiguous_bytes(const std::vector<uint8_t> &storage,
                                 std::size_t head_index,
                                 std::size_t used_size) noexcept {
  if (storage.empty() || used_size == 0) {
    return {};
  }
  const std::size_t contiguous_size =
      std::min(used_size, storage.size() - head_index);
  return std::span<const uint8_t>(
      storage.data() + static_cast<std::ptrdiff_t>(head_index), contiguous_size);
}

} // namespace mqtt

