#include "session_manager/session_takeover_handler.h"

namespace mqtt {

void SessionTakeoverHandler::register_connection(
    std::string_view client_id, std::function<void()> close_callback) {
  active_connections_.insert_or_assign(std::string(client_id),
                                       std::move(close_callback));
}

void SessionTakeoverHandler::unregister_connection(std::string_view client_id) {
  active_connections_.erase(std::string(client_id));
}

bool SessionTakeoverHandler::takeover_if_exists(std::string_view client_id) {
  const auto iter = active_connections_.find(std::string(client_id));
  if (iter == active_connections_.end()) {
    return false;
  }
  // Invoke the close callback — this sends DISCONNECT(0x8E) to the old
  // connection and closes it.
  if (iter->second) {
    iter->second();
  }
  active_connections_.erase(iter);
  return true;
}

bool SessionTakeoverHandler::is_active(
    std::string_view client_id) const noexcept {
  return active_connections_.contains(std::string(client_id));
}

std::size_t SessionTakeoverHandler::size() const noexcept {
  return active_connections_.size();
}

} // namespace mqtt
