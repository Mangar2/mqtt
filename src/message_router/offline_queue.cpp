#include "message_router/offline_queue.h"

#include "message_router/message_router_error.h"

namespace mqtt {

OfflineQueue::OfflineQueue(std::size_t max_size) noexcept
    : max_size_(max_size) {}

void OfflineQueue::enqueue(std::string_view client_id, const Message &msg) {
  auto &queue = queues_[std::string(client_id)];

  if (queue.size() >= max_size_) {
    throw MessageRouterException(MessageRouterError::QueueFull,
                                 "offline queue size limit reached");
  }

  queue.push_back(QueuedMessage{
      .message = msg,
      .enqueue_time = std::chrono::steady_clock::now(),
  });
}

std::vector<QueuedMessage> OfflineQueue::drain(std::string_view client_id) {
  auto iter = queues_.find(std::string(client_id));
  if (iter == queues_.end()) {
    return {};
  }

  std::vector<QueuedMessage> result(
      std::make_move_iterator(iter->second.begin()),
      std::make_move_iterator(iter->second.end()));
  queues_.erase(iter);
  return result;
}

std::size_t OfflineQueue::size(std::string_view client_id) const noexcept {
  auto iter = queues_.find(std::string(client_id));
  if (iter == queues_.end()) {
    return 0U;
  }
  return iter->second.size();
}

void OfflineQueue::purge(std::string_view client_id) {
  queues_.erase(std::string(client_id));
}

} // namespace mqtt
