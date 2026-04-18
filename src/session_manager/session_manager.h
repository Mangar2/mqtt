#pragma once

/**
 * @file session_manager.h
 * @brief SessionManager — top-level session lifecycle coordinator
 * (Module 10.1).
 */

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data_model/packet/connect_packet.h"
#include "session_manager/session_expiry_scheduler.h"
#include "session_manager/session_open_result.h"
#include "session_manager/session_takeover_handler.h"
#include "store/inflight_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"

namespace mqtt {

class StructuredTracer;

/**
 * @brief Coordinates session lifecycle for all MQTT 5.0 clients (Module 10.1).
 *
 * Combines the Session Lifecycle Controller (10.1), Session Takeover Handler
 * (10.2), and Session Expiry Scheduler (10.3) into a single coherent API
 * that can be called from the connection-handling layer.
 *
 * Thread safety: none — external synchronisation required.
 */
class SessionManager {
public:
  /**
   * @brief Construct the session manager.
   *
   * @param session_store     Store for session state records (4.3).
   * @param subscription_store Store for all active subscriptions (4.1).
   * @param inflight_store    Store for in-flight QoS 1/2 entries (4.4).
   * @param takeover_handler  Handler for Client ID collision (10.2).
   * @param expiry_scheduler  Scheduler for session expiry timers (10.3).
   */
  SessionManager(SessionStore &session_store,
                 SubscriptionStore &subscription_store,
                 InflightStore &inflight_store,
                 SessionTakeoverHandler &takeover_handler,
                 SessionExpiryScheduler &expiry_scheduler);

  /**
   * @brief Process a client CONNECT event (10.1.1–10.1.3, 10.2, 10.3.2).
   *
   * Steps performed in order:
   * 1. Validate `connect.client_id` — throws on empty value.
   * 2. Attempt session takeover (10.2).
   * 3. If `clean_start == true`: discard any existing session and create
   *    a fresh one. `session_present` is `false`.
   * 4. If `clean_start == false`: resume existing session when found
   *    (`session_present = true`, expiry timer cancelled), otherwise create
   *    a fresh session (`session_present = false`).
   * 5. Register the connection close callback in `SessionTakeoverHandler`.
   *
   * @param connect        Decoded CONNECT packet.
   * @param close_callback Callback that closes this connection (used for
   *        future takeover by another client with the same Client ID).
   * @return `SessionOpenResult` carrying `session_present` and
   *         `takeover_occurred`.
   * @throws SessionManagerException(InvalidClientId) when
   *         `connect.client_id.value` is empty.
   */
  SessionOpenResult handle_connect(const ConnectPacket &connect,
                                   std::function<void()> close_callback);

  /**
   * @brief Process a client disconnect event (10.1.4, 10.3.1).
   *
   * Steps performed:
   * 1. Unregisters the connection from `SessionTakeoverHandler`.
   * 2. Computes the effective expiry interval:
   *    @p expiry_override takes precedence; falls back to the stored
   *    `session_expiry_interval`.
   * 3. If effective expiry == 0: immediately removes the session and all
   *    associated subscriptions and inflight entries.
   * 4. Otherwise: marks the session as disconnected in `SessionStore` and
   *    schedules an expiry timer.
   *
   * @param client_id       Client identifier of the disconnecting session.
   * @param expiry_override Optional override for `session_expiry_interval`
   *        (sent in the client's DISCONNECT properties).
   * @param now             Current time, used as the disconnect timestamp.
   */
  void handle_disconnect(std::string_view client_id,
                         std::optional<uint32_t> expiry_override,
                         std::chrono::steady_clock::time_point now);

  /**
   * @brief Remove all sessions that have exceeded their expiry deadline
   * (10.3.3).
   *
   * For each expired session all associated inflight entries, subscriptions,
   * and the session record itself are removed.  Expiry timer entries are
   * cancelled.
   *
   * @param now Current reference time for deadline evaluation.
   * @return List of Client IDs whose sessions were cleaned up.
   */
  std::vector<std::string>
  cleanup_expired(std::chrono::steady_clock::time_point now);

  /**
   * @brief Attach optional structured tracer for runtime diagnostics.
   *
   * @param tracer Tracer instance; nullptr disables trace emission.
   */
  void set_tracer(StructuredTracer *tracer) noexcept;

  /**
   * @brief Return the shared inflight store used by this manager.
   *
   * This is used by the connection layer to construct per-client
   * `ClientSession` instances that share the broker-wide inflight state.
   */
  [[nodiscard]] InflightStore &inflight_store() noexcept;

private:
  /// Remove all data associated with a session (subscriptions + inflight +
  /// session record).
  void remove_session_data(std::string_view client_id);

  SessionStore &session_store_;
  SubscriptionStore &subscription_store_;
  InflightStore &inflight_store_;
  SessionTakeoverHandler &takeover_handler_;
  SessionExpiryScheduler &expiry_scheduler_;
  StructuredTracer *structured_tracer_{nullptr};
};

} // namespace mqtt
