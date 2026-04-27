#pragma once

/**
 * @file reconnect_controller.h
 * @brief Client-side reconnect controller with backoff scheduling.
 */

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "client/connection_negotiator.h"

namespace mqtt {

/**
 * @brief Trigger source for reconnect handling.
 */
enum class ReconnectTrigger : uint8_t {
  TransportError,  ///< Transport read/write or socket failure.
  KeepAliveTimeout, ///< Missing keep-alive response.
  UserInitiated,   ///< Local user-driven disconnect (no auto reconnect).
};

/**
 * @brief Current reconnect controller state.
 */
enum class ReconnectState : uint8_t {
  Connected,      ///< Connection healthy; no reconnect pending.
  WaitingForRetry,///< Waiting for next reconnect deadline.
  Reconnecting,   ///< Reconnect attempt currently in progress.
  Disabled,       ///< Auto reconnect disabled by user disconnect.
};

/**
 * @brief Backoff policy for reconnect retries.
 */
struct ReconnectBackoffPolicy {
  std::chrono::milliseconds initial_delay{std::chrono::milliseconds{1000}};
  std::chrono::milliseconds max_delay{std::chrono::milliseconds{30000}};
  double multiplier{2.0};
};

/**
 * @brief Result from one reconnect controller tick.
 */
struct ReconnectTickResult {
  bool attempted{false};
  bool reconnected{false};
  std::optional<std::chrono::steady_clock::time_point> next_retry_at;
  std::optional<std::string> error_message;
};

/**
 * @brief Orchestrates automatic reconnect attempts with configurable backoff.
 *
 * The controller is driven externally:
 * - call `on_connection_lost(...)` when transport/keep-alive detects a drop,
 * - call `tick(...)` regularly from an event loop,
 * - call `mark_connected()` after initial connect success.
 *
 * On successful reconnect the controller invokes restore callbacks for
 * session state and QoS state.
 */
class ReconnectController {
public:
  using Clock = std::chrono::steady_clock;
  using NegotiateFn = std::function<ConnectionNegotiationResult()>;
  using RestoreSessionFn =
      std::function<void(const ConnectionNegotiationResult &)>;
  using RestoreQosFn = std::function<void()>;

  /**
   * @brief Construct reconnect controller.
   * @param backoff_policy Retry backoff parameters.
   */
  explicit ReconnectController(ReconnectBackoffPolicy backoff_policy);

  /**
   * @brief Set reconnect negotiation callback.
   * @param negotiate_callback Callback performing dial/negotiate.
   */
  void set_negotiate_callback(NegotiateFn negotiate_callback) noexcept;

  /**
   * @brief Set session-restore callback.
   * @param restore_session_callback Callback restoring subscriptions/session state.
   */
  void set_restore_session_callback(
      RestoreSessionFn restore_session_callback) noexcept;

  /**
   * @brief Set QoS-restore callback.
   * @param restore_qos_callback Callback restoring QoS inflight engines.
   */
  void set_restore_qos_callback(RestoreQosFn restore_qos_callback) noexcept;

  /**
   * @brief Mark connection as healthy and reset reconnect backoff.
   */
  void mark_connected() noexcept;

  /**
   * @brief Notify controller about a connection loss trigger.
   * @param trigger Disconnect source.
   * @param now Event timestamp.
   */
  void on_connection_lost(ReconnectTrigger trigger,
                          Clock::time_point now = Clock::now()) noexcept;

  /**
   * @brief Drive reconnect state machine one step.
   * @param now Current time.
   * @return Tick result with attempt and status details.
   */
  [[nodiscard]] ReconnectTickResult tick(Clock::time_point now = Clock::now());

  /**
   * @brief Return current reconnect state.
   */
  [[nodiscard]] ReconnectState state() const noexcept;

  /**
   * @brief Return number of failed attempts since last successful connect.
   */
  [[nodiscard]] std::size_t failed_attempts() const noexcept;

  /**
   * @brief Return currently scheduled retry delay.
   */
  [[nodiscard]] std::chrono::milliseconds current_delay() const noexcept;

  /**
   * @brief Return next retry deadline when waiting.
   */
  [[nodiscard]] std::optional<Clock::time_point> next_retry_at() const noexcept;

  /**
   * @brief Return last reconnect error message.
   */
  [[nodiscard]] std::optional<std::string> last_error_message() const;

private:
  [[nodiscard]] std::chrono::milliseconds sanitize_initial_delay(
      std::chrono::milliseconds delay) const noexcept;
  [[nodiscard]] std::chrono::milliseconds sanitize_max_delay(
      std::chrono::milliseconds delay) const noexcept;
  [[nodiscard]] double sanitize_multiplier(double value) const noexcept;
  void schedule_next_retry(Clock::time_point now) noexcept;
  [[nodiscard]] bool should_attempt_reconnect(Clock::time_point now) const noexcept;

  ReconnectBackoffPolicy backoff_policy_;
  ReconnectState state_{ReconnectState::Connected};
  std::chrono::milliseconds current_delay_{std::chrono::milliseconds{0}};
  std::optional<Clock::time_point> next_retry_at_;
  std::size_t failed_attempts_{0U};
  std::optional<std::string> last_error_message_;

  NegotiateFn negotiate_callback_{};
  RestoreSessionFn restore_session_callback_{};
  RestoreQosFn restore_qos_callback_{};
};

} // namespace mqtt
