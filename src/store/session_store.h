#pragma once

/**
 * @file session_store.h
 * @brief In-memory session store (Module 4.3).
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/session/session_state.h"

namespace mqtt {

/**
 * @brief In-memory store for MQTT 5.0 session state (Module 4.3).
 *
 * Keyed by `client_id`.  Maintains a companion disconnect-time map so that
 * `expired_sessions()` can evaluate the `session_expiry_interval` of each
 * disconnected session.
 *
 * Throws `StoreException` for unrecoverable violations; see `store_error.h`.
 */
class SessionStore {
public:
  /**
   * @brief Create a new session (4.3.1).
   *
   * @param session Session to store; `session.client_id.value` must be
   * non-empty.
   * @throws StoreException(SessionAlreadyExists) if a session with the same
   *         client ID is already present.
   */
  void create(const SessionState &session);

  /**
   * @brief Load a session by client ID (4.3.2).
   *
   * @param client_id Client identifier to look up.
   * @return `std::optional<SessionState>` — present if found, empty otherwise.
   */
  [[nodiscard]] std::optional<SessionState>
  load(std::string_view client_id) const;

  /**
   * @brief Delete a session and its disconnect timestamp (4.3.3).
   *
   * Does nothing when @p client_id is not present.
   *
   * @param client_id Client identifier of the session to remove.
   */
  void remove(std::string_view client_id);

  /**
   * @brief Record the time at which a session was disconnected.
   *
   * Required to evaluate `session_expiry_interval` in `expired_sessions()`.
   * Must be called when the connection associated with @p client_id closes.
   *
   * @param client_id Identifier of the disconnected client.
   * @param timestamp Wall-clock time of the disconnect event.
   */
  void mark_disconnected(std::string_view client_id,
                         std::chrono::steady_clock::time_point timestamp);

  /**
   * @brief Return all sessions that have exceeded their expiry interval
   * (4.3.4).
   *
   * Expiry rules (MQTT 5.0 §3.1.2.11.2):
   * - `session_expiry_interval == 0`:           expired immediately on
   * disconnect.
   * - `session_expiry_interval == 0xFFFF'FFFF`: never expires.
   * - Otherwise: expires `session_expiry_interval` seconds after disconnect.
   *
   * Sessions without a recorded disconnect timestamp (i.e. still connected or
   * never disconnected) are excluded from the result.
   *
   * @param now The current time used as the expiry reference point.
   * @return Vector of expired `SessionState` values (copies).
   */
  [[nodiscard]] std::vector<SessionState>
  expired_sessions(std::chrono::steady_clock::time_point now) const;

  /**
   * @brief Return copies of all stored sessions.
   *
   * Used by the persistence layer to snapshot the full session set.
   *
   * @return Vector containing every `SessionState` in the store.
   */
  [[nodiscard]] std::vector<SessionState> all() const;

  /**
   * @brief Return the number of stored sessions.
   * @return Session count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

  /**
   * @brief Check whether a session exists for the given client ID.
   * @param client_id Client identifier to look up.
   * @return `true` if a session is present, `false` otherwise.
   */
  [[nodiscard]] bool contains(std::string_view client_id) const noexcept;

private:
  std::unordered_map<std::string, SessionState>
      sessions_; ///< client_id → session state.
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      disconnect_times_; ///< client_id → time of last disconnect.
};

} // namespace mqtt
