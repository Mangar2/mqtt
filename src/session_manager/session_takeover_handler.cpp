#include "session_manager/session_takeover_handler.h"

namespace mqtt {

void SessionTakeoverHandler::register_connection(
    std::string_view client_id, std::function<void()> close_callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_connections_.insert_or_assign(std::string(client_id),
                                       std::move(close_callback));
}

void SessionTakeoverHandler::unregister_connection(std::string_view client_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_connections_.erase(std::string(client_id));
}

bool SessionTakeoverHandler::takeover_if_exists(std::string_view client_id) {
  std::function<void()> close_callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = active_connections_.find(std::string(client_id));
    if (iter == active_connections_.end()) {
      return false;
    }
    close_callback = iter->second;
    active_connections_.erase(iter);
  }

  // Invoke the close callback outside the lock to avoid lock re-entrancy.
  if (close_callback) {
    close_callback();
  }

  return true;
}

bool SessionTakeoverHandler::is_active(
    std::string_view client_id) const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_connections_.contains(std::string(client_id));
}

std::size_t SessionTakeoverHandler::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_connections_.size();
}

} // namespace mqtt
