#pragma once

/**
 * @file broker.h
 * @brief Broker — MQTT 5.0 broker orchestrator that wires all modules together
 *        and controls startup / shutdown (Module 15.2 + 15.3).
 */

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "auth/anonymous_authenticator.h"
#include "auth/authenticator.h"
#include "auth/password_authenticator.h"
#include "authz/acl_engine.h"
#include "authz/acl_loader.h"
#include "broker/broker_config.h"
#include "data_model/message/message.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_router.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "network/tcp_listener.h"
#include "persistence/inflight_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"
#include "session_manager/session_expiry_scheduler.h"
#include "session_manager/session_manager.h"
#include "session_manager/session_takeover_handler.h"
#include "store/inflight_store.h"
#include "store/retained_message_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"
#include "will_manager/will_delay_timer.h"
#include "will_manager/will_publisher.h"
#include "will_manager/will_store.h"

namespace mqtt {

/**
 * @brief Top-level broker orchestrator for the MQTT 5.0 broker (Module 15).
 *
 * `Broker` owns every module object, wires their dependencies, and controls
 * the full lifecycle:
 *
 * 1. **startup()** — instantiate modules, load persistence, open TCP
 *    listeners, install signal handlers (15.3.1).
 * 2. **shutdown()** — close listeners, flush persistence (15.3.2).
 * 3. **Signal handling** — static `install_signal_handlers()` registers C
 *    signal handlers for SIGTERM and SIGINT; poll
 *    `Broker::shutdown_requested()` in the main loop (15.3.3).
 *
 * Connection handlers call `register_connection()` when a client connects
 * (so the message router can deliver to online clients) and
 * `unregister_connection()` when it disconnects.
 *
 * Thread safety: `startup()` / `shutdown()` / `register_connection()` /
 * `unregister_connection()` are not thread-safe; call from a single thread.
 * `shutdown_requested()` is thread-safe (reads an atomic flag).
 */
class Broker {
public:
  /**
   * @brief Callback type used by the message router to deliver a message
   *        to an online client.
   *
   * @param msg  Message ready for delivery.
   */
  using SendFn = std::function<void(const Message &)>;

  /**
   * @brief Construct a Broker with the given configuration.
   *
   * Module objects are NOT created here — call `startup()` to initialise
   * the broker.
   *
   * @param config Validated `BrokerConfig`; stores a copy.
   */
  explicit Broker(BrokerConfig config);

  /**
   * @brief Destroy the broker.
   *
   * Calls `shutdown()` if the broker is still running.
   */
  ~Broker();

  Broker(const Broker &) = delete;
  Broker &operator=(const Broker &) = delete;

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  /**
   * @brief Start the broker (15.3.1).
   *
   * Sequence:
   * 1. Throw `BrokerException(AlreadyRunning)` if already started.
   * 2. Instantiate all module objects in dependency order (15.2.1).
   * 3. Configure the authenticator according to `allow_anonymous` (15.2.3).
   * 4. Load ACL rules; always add a broker-internal allow-all rule so that
   *    will messages pass publish authorisation.
   * 5. If `persistence_enabled`: load snapshots and populate stores
   *    (15.2.2).
   * 6. Open TCP listener(s) (15.3.1).
   * 7. Set `running_` to `true`.
   *
   * @throws BrokerException(AlreadyRunning) if already running.
   * @throws NetworkException if a TCP listener cannot be opened.
   */
  void startup();

  /**
   * @brief Stop the broker (15.3.2).
   *
   * Sequence:
   * 1. Return immediately if not running.
   * 2. Close all TCP listeners.
   * 3. If `persistence_enabled`: flush in-memory stores to snapshot files.
   * 4. Set `running_` to `false`.
   *
   * `noexcept` — errors during persistence flush are silently swallowed so
   * that shutdown always completes.
   */
  void shutdown() noexcept;

  /**
   * @brief Return whether the broker is currently running.
   * @return `true` between a successful `startup()` and `shutdown()`.
   */
  [[nodiscard]] bool is_running() const noexcept;

  // ── Module accessors (valid after startup()) ──────────────────────────────

  /// @return Reference to the `SessionManager` (Module 10).
  [[nodiscard]] SessionManager &session_manager() noexcept;

  /// @return Reference to the `MessageRouter` (Module 12).
  [[nodiscard]] MessageRouter &message_router() noexcept;

  /// @return Reference to the active `IAuthenticator` (Module 8).
  [[nodiscard]] IAuthenticator &authenticator() noexcept;

  /// @return Reference to the `AclEngine` (Module 9).
  [[nodiscard]] AclEngine &acl_engine() noexcept;

  /// @return Reference to the `WillPublisher` (Module 11).
  [[nodiscard]] WillPublisher &will_publisher() noexcept;

  /// @return Reference to the `SubscriptionStore` (Module 4).
  [[nodiscard]] SubscriptionStore &subscription_store() noexcept;

  // ── Connection registration ────────────────────────────────────────────────

  /**
   * @brief Register an active connection with the broker.
   *
   * Must be called after a client successfully completes the CONNECT
   * handshake.  The @p send_fn is invoked by the message router to deliver
   * messages to the client.
   *
   * @param client_id  Client identifier.
   * @param send_fn    Callback that sends a message to this client.
   */
  void register_connection(std::string_view client_id, SendFn send_fn);

  /**
   * @brief Unregister a connection (e.g. on disconnect or close).
   *
   * No-op when @p client_id is not registered.
   *
   * @param client_id Client identifier to remove.
   */
  void unregister_connection(std::string_view client_id) noexcept;

  // ── Signal handling (15.3.3) ──────────────────────────────────────────────

  /**
   * @brief Install C signal handlers for SIGTERM and SIGINT (15.3.3).
   *
   * Both signals set `shutdown_requested_` to `true`.  Call this once
   * from the main thread before entering the main loop.
   */
  static void install_signal_handlers() noexcept;

  /**
   * @brief Return whether a shutdown signal has been received (15.3.3).
   *
   * Thread-safe (reads an atomic flag).
   *
   * @return `true` when SIGTERM or SIGINT has been caught.
   */
  [[nodiscard]] static bool shutdown_requested() noexcept;

private:
  // ── Internal helpers ──────────────────────────────────────────────────────

  /// Instantiate all module objects (15.2.1).
  void create_modules();

  /// Load persistence snapshots into in-memory stores (15.2.2).
  void load_persistence();

  /// Write in-memory stores to persistence snapshot files (15.3.2).
  void flush_persistence() noexcept;

  /// Open TCP listener(s) based on the config (15.3.1).
  void open_listeners();

  /// Close all open TCP listeners (15.3.2).
  void close_listeners() noexcept;

  /// Static C signal handler (15.3.3).
  static void handle_signal(int sig) noexcept;

  // ── State ─────────────────────────────────────────────────────────────────

  BrokerConfig config_; ///< Broker configuration (copy from constructor).
  bool running_;        ///< True between startup() and shutdown().

  // ── Persistence (Module 13) ───────────────────────────────────────────────

  std::unique_ptr<SessionPersistence> session_persistence_;
  std::unique_ptr<RetainedMessagePersistence> retained_persistence_;
  std::unique_ptr<InflightPersistence> inflight_persistence_;

  // ── In-memory stores (Module 4) ───────────────────────────────────────────

  std::unique_ptr<SessionStore> session_store_;
  std::unique_ptr<RetainedMessageStore> retained_store_;
  std::unique_ptr<SubscriptionStore> subscription_store_;
  std::unique_ptr<InflightStore> inflight_store_;

  // ── Auth (Module 8) ───────────────────────────────────────────────────────

  std::unique_ptr<AnonymousAuthenticator> anon_auth_;
  std::unique_ptr<PasswordAuthenticator> pass_auth_;
  IAuthenticator *active_auth_; ///< Non-owning pointer.

  // ── AuthZ (Module 9) ──────────────────────────────────────────────────────

  std::unique_ptr<AclEngine> acl_engine_;
  std::unique_ptr<AclLoader> acl_loader_;

  // ── Session Manager (Module 10) ───────────────────────────────────────────

  std::unique_ptr<SessionTakeoverHandler> takeover_handler_;
  std::unique_ptr<SessionExpiryScheduler> expiry_scheduler_;
  std::unique_ptr<SessionManager> session_manager_;

  // ── Will Manager (Module 11) ──────────────────────────────────────────────

  std::unique_ptr<WillStore> will_store_;
  std::unique_ptr<WillDelayTimer> will_delay_timer_;
  std::unique_ptr<WillPublisher> will_publisher_;

  // ── Message Router (Module 12) ────────────────────────────────────────────

  std::unique_ptr<InboundPublishProcessor> publish_processor_;
  std::unique_ptr<OfflineQueue> offline_queue_;
  std::unique_ptr<SharedSubscriptionDispatcher> shared_dispatcher_;
  std::unique_ptr<MessageRouter> message_router_;

  // ── Network (Module 6) ────────────────────────────────────────────────────

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;

  // ── Connection tracking ───────────────────────────────────────────────────

  std::unordered_map<std::string, SendFn> active_connections_;

  // ── Signal flag ───────────────────────────────────────────────────────────

  /// Set to `true` by the C signal handler on SIGTERM / SIGINT.
  static std::atomic<bool> shutdown_requested_;
};

} // namespace mqtt
