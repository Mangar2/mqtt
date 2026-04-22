#include "network/connection_table.h"

#include <memory>
#include <mutex>
#include <shared_mutex>

#include "connection/connection_session.h"

namespace mqtt {

bool ConnectionTable::add(int socket_fd, ConnectionSlot slot,
                          std::unique_ptr<ConnectionSession> session) {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);

  if (entries_.contains(socket_fd)) {
    return false;
  }
  auto entry = std::make_unique<Entry>(
      Entry{.slot = std::move(slot), .session = std::move(session)});
  entries_.emplace(socket_fd, std::move(entry));
  return true;
}

bool ConnectionTable::remove(int socket_fd) {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);
  return entries_.erase(socket_fd) > 0;
}

ConnectionTable::Entry *ConnectionTable::find(int socket_fd) {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  const auto iter = entries_.find(socket_fd);
  if (iter == entries_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const ConnectionTable::Entry *ConnectionTable::find(int socket_fd) const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  const auto iter = entries_.find(socket_fd);
  if (iter == entries_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

void ConnectionTable::clear() {
  std::unique_lock<std::shared_mutex> lock_guard(mutex_);
  entries_.clear();
}

std::size_t ConnectionTable::size() const noexcept {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  return entries_.size();
}

std::vector<SocketHandle> ConnectionTable::snapshot_socket_handles() const {
  std::shared_lock<std::shared_mutex> lock_guard(mutex_);
  std::vector<SocketHandle> socket_handles;
  socket_handles.reserve(entries_.size());
  for (const auto &entry : entries_) {
    socket_handles.push_back(entry.second->slot.fd());
  }
  return socket_handles;
}

} // namespace mqtt
