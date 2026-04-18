/**
 * @file outbound_queue.cpp
 * @brief OutboundQueue implementation (Module 20.1).
 */

#include "outbound_queue/outbound_queue.h"

#include <utility>

namespace mqtt {

OutboundQueue::OutboundQueue(std::size_t max_depth)
    : max_depth_{max_depth} {}

bool OutboundQueue::push(Message msg) {
  if (stopped_.load(std::memory_order_acquire)) {
    return false;
  }

  std::lock_guard<std::mutex> lock_guard(mutex_);

  if (queue_.size() >= max_depth_) {
    return false;
  }

  queue_.push(std::move(msg));
  return true;
}

std::optional<Message> OutboundQueue::try_pop() {
  std::lock_guard<std::mutex> lock_guard(mutex_);

  if (queue_.empty()) {
    return std::nullopt;
  }

  Message msg = std::move(queue_.front());
  queue_.pop();
  return msg;
}

void OutboundQueue::stop() { stopped_.store(true, std::memory_order_release); }

bool OutboundQueue::is_stopped() const noexcept {
  return stopped_.load(std::memory_order_acquire);
}

bool OutboundQueue::is_empty() const noexcept {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  return queue_.empty();
}

std::size_t OutboundQueue::size() const noexcept {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  return queue_.size();
}

} // namespace mqtt
