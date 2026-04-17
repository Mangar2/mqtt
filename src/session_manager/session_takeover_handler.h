#pragma once

/**
 * @file session_takeover_handler.h
 * @brief SessionTakeoverHandler — tracks active connections and handles
 * Client ID collision (Module 10.2).
 */

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mqtt {

/**
 * @brief Tracks currently-active client connections and handles session
 * takeover when a new connection arrives with a Client ID that is already
 * in use (Module 10.2).
 *
 * Each active connection registers a zero-argument close callback.  When
 * `takeover_if_exists` is called for an already-registered Client ID the old
 * callback is invoked (causing the old connection to close with Reason Code
 * 0x8E – SessionTakenOver) and the old entry is removed.
 *
 * The caller is responsible for subsequently registering the new connection
 * via `register_connection`.
 *
 * Thread safety: none — external synchronisation required.
 */
class SessionTakeoverHandler {
public:
  /**
   * @brief Register an active connection (10.2.1).
   *
   * @param client_id     Client identifier of the connection.
   * @param close_callback Callable invoked by `takeover_if_exists` to
   *        forcibly close this connection.
   */
  void register_connection(std::string_view client_id,
                           std::function<void()> close_callback);

  /**
   * @brief Unregister a connection when it closes normally (10.2.1).
   *
   * No-op when @p client_id is not registered.
   *
   * @param client_id Client identifier of the closing connection.
   */
  void unregister_connection(std::string_view client_id);

  /**
   * @brief Displace an existing connection if one is registered (10.2.2).
   *
   * When an entry exists for @p client_id:
   * - The registered close callback is invoked.
   * - The entry is removed from the active map.
   *
   * @param client_id Client identifier of the incoming connection.
   * @return `true` if an old connection was displaced; `false` otherwise.
   */
  bool takeover_if_exists(std::string_view client_id);

  /**
   * @brief Check whether a connection is currently active (10.2.1).
   *
   * @param client_id Client identifier to test.
   * @return `true` when a connection is registered for @p client_id.
   */
  [[nodiscard]] bool is_active(std::string_view client_id) const noexcept;

  /**
   * @brief Return the number of currently-registered connections.
   * @return Active connection count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  /// Map from client_id → close callback.
  std::unordered_map<std::string, std::function<void()>> active_connections_;
};

} // namespace mqtt
