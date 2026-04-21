#pragma once

/**
 * @file will_publisher.h
 * @brief WillPublisher — orchestrates will storage, timers, and publication
 * (Module 11.3).
 */

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

#include "data_model/message/message.h"
#include "data_model/reason_code/reason_code.h"
#include "will_manager/will_delay_timer.h"
#include "will_manager/will_store.h"

namespace mqtt {

/**
 * @brief Coordinates Will Message storage, delay timers, and publication
 * (Module 11.3).
 *
 * Integrates `WillStore` (11.1) and `WillDelayTimer` (11.2) to implement the
 * full will lifecycle:
 *
 * 1. **on_connect** — stores the will when a client connects.
 * 2. **on_reconnect** — cancels the delay timer when the client reconnects.
 * 3. **on_disconnect** — suppresses the will (Reason 0x00) or arms the delay
 *    timer (any other reason, including 0x04).
 * 4. **on_connection_lost** — arms the delay timer when no DISCONNECT was
 * received.
 * 5. **on_session_expired** — publishes the will immediately if the session
 *    expires before the delay timer fires.
 * 6. **publish_due** — publishes all wills whose delay timer has elapsed.
 *
 * Thread safety: none — external synchronisation required.
 */
class WillPublisher {
public:
  /// Callback invoked to publish a will message into the routing pipeline.
  using PublishCallback = std::function<void(const WillMessage &)>;

  /**
   * @brief Construct a WillPublisher.
   *
   * @param will_store   Store holding the `WillMessage` records (11.1).
   * @param delay_timer  Timer tracking per-client will-delay intervals (11.2).
   * @param publish_fn   Callback invoked to route the will message (11.3.1).
   */
  WillPublisher(WillStore &will_store, WillDelayTimer &delay_timer,
                PublishCallback publish_fn);

  /**
   * @brief Store the Will Message when a client connects (11.1.1).
   *
   * @param client_id Client identifier.
   * @param will      Will Message extracted from the CONNECT packet.
   */
  void on_connect(std::string_view client_id, const WillMessage &will);

  /**
   * @brief Cancel the delay timer when the client reconnects (11.2.2).
   *
   * Called before `on_connect` when a client resumes an existing session.
   * No-op when no timer is pending for @p client_id.
   *
   * @param client_id Client identifier.
   */
  void on_reconnect(std::string_view client_id);

  /**
   * @brief Handle a client DISCONNECT packet (11.3.2, 11.3.3).
   *
   * - Reason `0x00` (Normal Disconnection): removes the will and cancels any
   *   pending timer.  Will is suppressed.
   * - Reason `0x04` (DisconnectWithWill) or any other reason: arms the delay
   *   timer.  If `will.delay_interval == 0`, publishes and removes the will
   *   immediately.
   *
   * @param client_id Client identifier.
   * @param reason    Reason Code from the DISCONNECT packet.
   * @param now       Current time, used as the disconnect timestamp.
   */
  void on_disconnect(std::string_view client_id, ReasonCode reason,
                     std::chrono::steady_clock::time_point now);

  /**
   * @brief Handle an abrupt connection loss (no DISCONNECT received).
   *
   * Arms the delay timer using the stored will's `delay_interval`.
   * If `will.delay_interval == 0`, publishes and removes the will immediately.
   * No-op when no will is stored for @p client_id.
   *
   * @param client_id Client identifier.
   * @param now       Wall-clock time of the connection close event.
   */
  void on_connection_lost(std::string_view client_id,
                          std::chrono::steady_clock::time_point now);

  /**
   * @brief Publish the will immediately because the session has expired
   * (11.2.3).
   *
   * Called by the Session Manager when it cleans up an expired session.
   * If a will is stored or a timer is pending for @p client_id, the will is
   * published immediately and all associated state is removed.
   * No-op when no will is stored for @p client_id.
   *
   * @param client_id Client identifier of the expired session.
   */
  void on_session_expired(std::string_view client_id);

  /**
   * @brief Publish all wills whose delay timer has elapsed (11.2.1, 11.3.1).
   *
   * Collects all due timers, loads their wills from `WillStore`, invokes
   * `publish_fn` for each, then removes both the timer entry and the will.
   *
   * @param now Reference time for timer evaluation.
   */
  void publish_due(std::chrono::steady_clock::time_point now);

private:
  /// Arm the delay timer; return will for immediate publish when delay is 0.
  [[nodiscard]] std::optional<WillMessage>
  arm_timer(std::string_view client_id,
            std::chrono::steady_clock::time_point now);

  mutable std::mutex mutex_;
  WillStore &will_store_;       ///< Backing will store.
  WillDelayTimer &delay_timer_; ///< Backing delay timer.
  PublishCallback publish_fn_;  ///< Callback for routing the will message.
};

} // namespace mqtt
