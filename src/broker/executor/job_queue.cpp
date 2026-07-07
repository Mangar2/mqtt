#include "executor/job_queue.h"

#include <utility>

namespace mqtt {

void JobQueue::push(ConnectionJob job) {
  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
}

std::optional<ConnectionJob> JobQueue::pop_blocking() {
  std::unique_lock<std::mutex> lock_guard(mutex_);
  cv_.wait(lock_guard, [this] {
    return shutdown_requested_ || !queue_.empty();
  });

  if (queue_.empty()) {
    return std::nullopt;
  }

  ConnectionJob job = std::move(queue_.front());
  queue_.pop_front();
  return job;
}

std::size_t JobQueue::size() const noexcept {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  return queue_.size();
}

void JobQueue::shutdown() {
  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    shutdown_requested_ = true;
  }
  cv_.notify_all();
}

} // namespace mqtt

