#include "network/connection_table.h"

#include <memory>
#include <mutex>
#include <shared_mutex>

namespace mqtt {

bool ConnectionTable::add(ConnectionSlot slot) {
  const int socket_fd = static_cast<int>(slot.fd());
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);

  if (slots_.contains(socket_fd)) {
    return false;
  }
  slots_.emplace(socket_fd,
                 std::make_unique<ConnectionSlot>(std::move(slot)));
  return true;
}

bool ConnectionTable::remove(int socket_fd) {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);
  return slots_.erase(socket_fd) > 0;
}

ConnectionSlot *ConnectionTable::find(int socket_fd) {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  const auto iter = slots_.find(socket_fd);
  if (iter == slots_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const ConnectionSlot *ConnectionTable::find(int socket_fd) const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  const auto iter = slots_.find(socket_fd);
  if (iter == slots_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

std::size_t ConnectionTable::size() const noexcept {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  return slots_.size();
}

std::vector<SocketHandle> ConnectionTable::snapshot_socket_handles() const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  std::vector<SocketHandle> socket_handles;
  socket_handles.reserve(slots_.size());
  for (const auto &entry : slots_) {
    socket_handles.push_back(entry.second->fd());
  }
  return socket_handles;
}

} // namespace mqtt
