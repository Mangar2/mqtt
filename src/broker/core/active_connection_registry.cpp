#include "broker/active_connection_registry.h"

namespace mqtt {

ConnectionUpsertResult
ActiveConnectionRegistry::upsert(std::string_view client_id,
                                 std::shared_ptr<OutboundQueue> queue,
                                 std::optional<int> fd) {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);

  ConnectionUpsertResult result;
  const std::string key(client_id);
  auto existing_it = active_connections_.find(key);
  if (existing_it != active_connections_.end()) {
    result.replaced_existing = true;
    result.previous_queue = existing_it->second.queue;
    result.previous_fd = existing_it->second.fd;
    existing_it->second.queue = std::move(queue);
    existing_it->second.fd = fd;
  } else {
    active_connections_.insert_or_assign(
        key, ActiveConnectionEntry{.queue = std::move(queue), .fd = fd});
  }

  result.active_connections = active_connections_.size();
  return result;
}

ConnectionRemoveResult ActiveConnectionRegistry::remove_if_matches(
    std::string_view client_id,
    const std::shared_ptr<OutboundQueue> &expected_queue) {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);

  ConnectionRemoveResult result;
  const std::string key(client_id);
  auto iter = active_connections_.find(key);
  if (iter == active_connections_.end()) {
    return result;
  }

  if (expected_queue && iter->second.queue != expected_queue) {
    return result;
  }

  result.removed = true;
  result.active_connections_before = active_connections_.size();
  result.removed_queue = std::move(iter->second.queue);
  result.removed_fd = iter->second.fd;
  active_connections_.erase(iter);
  return result;
}

std::shared_ptr<OutboundQueue>
ActiveConnectionRegistry::find(std::string_view client_id) const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);

  auto iter = active_connections_.find(std::string(client_id));
  if (iter == active_connections_.end()) {
    return nullptr;
  }
  return iter->second.queue;
}

bool ActiveConnectionRegistry::contains(std::string_view client_id) const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  return active_connections_.contains(std::string(client_id));
}

std::optional<int> ActiveConnectionRegistry::fd_for(
    std::string_view client_id) const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  const auto iter = active_connections_.find(std::string(client_id));
  if (iter == active_connections_.end()) {
    return std::nullopt;
  }
  return iter->second.fd;
}

std::size_t ActiveConnectionRegistry::size() const noexcept {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  return active_connections_.size();
}

std::vector<std::shared_ptr<OutboundQueue>>
ActiveConnectionRegistry::snapshot_queues() const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);

  std::vector<std::shared_ptr<OutboundQueue>> queues;
  queues.reserve(active_connections_.size());
  for (const auto &entry : active_connections_) {
    queues.push_back(entry.second.queue);
  }
  return queues;
}

} // namespace mqtt
