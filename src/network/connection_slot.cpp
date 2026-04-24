#include "network/connection_slot.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace mqtt {

ConnectionSlot::ConnectionSlot(SocketHandle socket_handle,
                               std::size_t write_capacity_bytes)
    : socket_handle_(socket_handle),
      write_storage_(std::min(std::max<std::size_t>(1U, write_capacity_bytes),
                              k_min_write_capacity)),
      write_limit_capacity_(std::max<std::size_t>(1U, write_capacity_bytes)),
      write_peak_window_started_at_(std::chrono::steady_clock::now()),
      last_write_activity_at_(write_peak_window_started_at_) {}

ConnectionSlot::ConnectionSlot(ConnectionSlot &&other) noexcept
    : socket_handle_(other.socket_handle_), phase_(other.phase_),
      write_storage_(std::move(other.write_storage_)),
      write_limit_capacity_(other.write_limit_capacity_),
      write_head_index_(other.write_head_index_),
      write_used_size_(other.write_used_size_),
      write_peak_window_bytes_(other.write_peak_window_bytes_),
      write_peak_window_started_at_(other.write_peak_window_started_at_),
      last_write_activity_at_(other.last_write_activity_at_) {
  other.socket_handle_ = k_invalid_socket;
  other.phase_ = ConnectionPhase::Closing;
  other.write_limit_capacity_ = 0;
  other.write_head_index_ = 0;
  other.write_used_size_ = 0;
  other.write_peak_window_bytes_ = 0;
}

ConnectionSlot &ConnectionSlot::operator=(ConnectionSlot &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  socket_handle_ = other.socket_handle_;
  phase_ = other.phase_;
  write_storage_ = std::move(other.write_storage_);
  write_limit_capacity_ = other.write_limit_capacity_;
  write_head_index_ = other.write_head_index_;
  write_used_size_ = other.write_used_size_;
  write_peak_window_bytes_ = other.write_peak_window_bytes_;
  write_peak_window_started_at_ = other.write_peak_window_started_at_;
  last_write_activity_at_ = other.last_write_activity_at_;

  other.socket_handle_ = k_invalid_socket;
  other.phase_ = ConnectionPhase::Closing;
  other.write_limit_capacity_ = 0;
  other.write_head_index_ = 0;
  other.write_used_size_ = 0;
  other.write_peak_window_bytes_ = 0;
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
  if (!ensure_write_capacity_for(data.size())) {
    return false;
  }
  const bool pushed =
      push_bytes(write_storage_, write_head_index_, write_used_size_, data);
  if (pushed) {
    last_write_activity_at_ = std::chrono::steady_clock::now();
    refresh_write_peak_window(last_write_activity_at_);
  }
  return pushed;
}

std::size_t ConnectionSlot::pop_write_bytes(std::size_t bytes_to_pop) noexcept {
  const std::size_t popped =
      pop_bytes(write_head_index_, write_used_size_, bytes_to_pop,
                write_storage_.size());
  if (popped > 0U) {
    refresh_write_peak_window(std::chrono::steady_clock::now());
  }
  return popped;
}

std::span<const uint8_t>
ConnectionSlot::write_contiguous_bytes() const noexcept {
  return contiguous_bytes(write_storage_, write_head_index_, write_used_size_);
}

void ConnectionSlot::maybe_trim_write_capacity(
    std::chrono::steady_clock::time_point now) noexcept {
  if (write_storage_.size() <= k_min_write_capacity || write_used_size_ > 0U) {
    return;
  }

  refresh_write_peak_window(now);
  if (now - last_write_activity_at_ < k_write_shrink_idle) {
    return;
  }

  constexpr std::size_t k_shrink_headroom_bytes = 4U * 1024U;
  const std::size_t target_capacity = std::max(
      k_min_write_capacity,
      std::min(write_limit_capacity_, write_peak_window_bytes_ + k_shrink_headroom_bytes));
  if (target_capacity >= write_storage_.size()) {
    return;
  }

  // Hysteresis: only shrink when at least 50% of current capacity can be freed.
  if (target_capacity * 2U > write_storage_.size()) {
    return;
  }

  reallocate_write_storage(target_capacity);
  write_peak_window_started_at_ = now;
  write_peak_window_bytes_ = write_used_size_;
  last_write_activity_at_ = now;
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

bool ConnectionSlot::ensure_write_capacity_for(
    std::size_t additional_bytes) noexcept {
  if (additional_bytes > write_limit_capacity_ - write_used_size_) {
    return false;
  }
  if (additional_bytes <= write_storage_.size() - write_used_size_) {
    refresh_write_peak_window(std::chrono::steady_clock::now());
    return true;
  }

  const std::size_t required_capacity = write_used_size_ + additional_bytes;
  std::size_t new_capacity = std::max(write_storage_.size(), k_min_write_capacity);
  while (new_capacity < required_capacity) {
    const std::size_t doubled = new_capacity * 2U;
    if (doubled <= new_capacity) {
      new_capacity = write_limit_capacity_;
      break;
    }
    new_capacity = std::min(write_limit_capacity_, doubled);
    if (new_capacity < required_capacity && new_capacity == write_limit_capacity_) {
      break;
    }
  }

  if (new_capacity < required_capacity) {
    return false;
  }

  reallocate_write_storage(new_capacity);
  last_write_activity_at_ = std::chrono::steady_clock::now();
  refresh_write_peak_window(last_write_activity_at_);
  return true;
}

void ConnectionSlot::reallocate_write_storage(std::size_t new_capacity) noexcept {
  if (new_capacity == write_storage_.size()) {
    return;
  }

  std::vector<uint8_t> new_storage(new_capacity);
  const std::span<const uint8_t> first_chunk =
      contiguous_bytes(write_storage_, write_head_index_, write_used_size_);
  const std::size_t first_size = first_chunk.size();
  if (first_size > 0U) {
    std::copy(first_chunk.begin(), first_chunk.end(), new_storage.begin());
  }

  const std::size_t remaining = write_used_size_ - first_size;
  if (remaining > 0U) {
    std::copy_n(write_storage_.begin(), static_cast<std::ptrdiff_t>(remaining),
                new_storage.begin() + static_cast<std::ptrdiff_t>(first_size));
  }

  write_storage_ = std::move(new_storage);
  write_head_index_ = 0U;
}

void ConnectionSlot::refresh_write_peak_window(
    std::chrono::steady_clock::time_point now) noexcept {
  if (now - write_peak_window_started_at_ >= k_write_peak_window) {
    write_peak_window_started_at_ = now;
    write_peak_window_bytes_ = write_used_size_;
  }
  if (write_used_size_ > write_peak_window_bytes_) {
    write_peak_window_bytes_ = write_used_size_;
  }
}

} // namespace mqtt

