#include "store/session_store.h"

#include <chrono>
#include <format>

#include "store/store_error.h"

namespace mqtt {

void SessionStore::create(const SessionState &session) {
  const std::string &cid = session.client_id.value;
  if (sessions_.contains(cid)) {
    throw StoreException(StoreError::SessionAlreadyExists,
                         std::format("session already exists: {}", cid));
  }
  sessions_.emplace(cid, session);
}

std::optional<SessionState>
SessionStore::load(std::string_view client_id) const {
  const auto iter = sessions_.find(std::string(client_id));
  if (iter == sessions_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

void SessionStore::remove(std::string_view client_id) {
  const std::string key(client_id);
  sessions_.erase(key);
  disconnect_times_.erase(key);
}

void SessionStore::mark_disconnected(
    std::string_view client_id,
    std::chrono::steady_clock::time_point timestamp) {
  disconnect_times_.insert_or_assign(std::string(client_id), timestamp);
}

std::vector<SessionState> SessionStore::expired_sessions(
    std::chrono::steady_clock::time_point now) const {
  std::vector<SessionState> result;

  for (const auto &[cid, state] : sessions_) {
    const auto dt_iter = disconnect_times_.find(cid);
    if (dt_iter == disconnect_times_.end()) {
      // Session has never been marked as disconnected — skip.
      continue;
    }

    const uint32_t expiry = state.session_expiry_interval;
    constexpr uint32_t k_never_expires = 0xFFFF'FFFFU;

    if (expiry == k_never_expires) {
      continue;
    }

    if (expiry == 0U) {
      // Expires immediately — always expired once disconnected.
      result.push_back(state);
      continue;
    }

    const auto deadline = dt_iter->second + std::chrono::seconds(expiry);
    if (now >= deadline) {
      result.push_back(state);
    }
  }

  return result;
}

std::size_t SessionStore::size() const noexcept { return sessions_.size(); }

bool SessionStore::contains(std::string_view client_id) const noexcept {
  return sessions_.contains(std::string(client_id));
}

} // namespace mqtt
