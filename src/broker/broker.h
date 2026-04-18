#pragma once

/**
 * @file broker.h
 * @brief Broker — MQTT 5.0 broker orchestrator that wires all modules together
 *        and controls startup / shutdown (Module 15.2 + 15.3).
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "auth/anonymous_authenticator.h"
#include "auth/auth_error.h"
#include "auth/authenticator.h"
#include "auth/enhanced_auth_handler.h"
#include "auth/password_authenticator.h"
#include "outbound_queue/outbound_queue.h"
#include "authz/acl_engine.h"
#include "authz/acl_loader.h"
#include "broker/broker_config.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/reason_code/reason_code.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_router.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/sys_topic_publisher.h"
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
 * @brief Full outcome of Broker CONNECT handling (Module 18.1).
 *
 * Returned by `Broker::handle_connect()` and used by the connection-handling
 * layer to build the outbound CONNACK packet or terminate the handshake on
 * failure.
 */
struct ConnectResult {
  AuthStatus auth_status{
      AuthStatus::Success};    ///< Authentication stage outcome.
  bool session_present{false}; ///< CONNACK Session Present flag.
  ReasonCode reason_code{ReasonCode::Success}; ///< Final connection outcome.
  std::optional<BinaryData>
      auth_data;           ///< AUTH payload when `auth_status == Continue`.
  std::string auth_method; ///< Negotiated auth method for enhanced auth.
  std::vector<Property> connack_properties; ///< CONNACK properties from broker
                                            ///< configuration.
  std::string client_id; ///< Final client identifier used for this connection.
};

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
 * Thread safety: all mutating Broker APIs take an internal exclusive lock
 * (`std::shared_mutex`) around shared mutable state.
 * `shutdown_requested()` is thread-safe (reads an atomic flag).
 */
class Broker {
public:


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

  //  Lifecycle

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

  //  Module accessors (valid after startup())

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

  /// @return Reference to the `StatisticsCollector` (Module 16).
  [[nodiscard]] StatisticsCollector &statistics_collector() noexcept;

  /**
   * @brief High-level CONNECT facade (Module 18.2).
   *
   * Sequence under one exclusive broker lock:
   * 1. Authenticate the CONNECT packet.
   * 2. Open or resume the session.
   * 3. Extract and store the Will Message (if present).
   * 4. Build CONNACK properties from broker configuration.
   * 5. Flush the offline queue when a previous session is resumed.
   *
   * Any recoverable handshake failure is reported via `reason_code`; the
   * method does not throw for expected protocol outcomes like auth failure or
   * invalid client ID.
   *
   * @param connect_packet CONNECT packet.
   * @param close_callback Connection close callback for session takeover.
   * @return `ConnectResult` with complete handshake outcome.
   */
  ConnectResult handle_connect(const ConnectPacket &connect_packet,
                               std::function<void()> close_callback);

  /**
   * @brief Continue an in-progress enhanced CONNECT authentication exchange.
   *
   * The connection layer calls this for incoming AUTH packets after
   * `handle_connect()` returned `AuthStatus::Continue`.
   *
   * - `Continue`: caller sends AUTH(0x18) and waits for next AUTH packet.
   * - `Success`: broker finalises session open and returns a successful
   *   `ConnectResult`.
   * - `Failure`: caller terminates handshake with returned reason code.
   *
   * @param client_id Client identifier tied to the in-progress exchange.
   * @param auth_packet Incoming AUTH packet.
   * @return Updated CONNECT workflow result.
   */
  ConnectResult handle_auth_packet(std::string_view client_id,
                                   const AuthPacket &auth_packet);

  /**
   * @brief Handle re-authentication for an already connected enhanced-auth
   * session.
   *
   * @param client_id Active client identifier.
   * @param auth_packet AUTH packet with `ReAuthenticate` reason code.
   * @return Re-authentication result from the configured authenticator.
   */
  AuthResult handle_reauthenticate(std::string_view client_id,
                                   const AuthPacket &auth_packet);

  /**
   * @brief Thread-safe wrapper for disconnect handling (Module 17.3.2).
   *
   * Applies will handling for DISCONNECT reason codes, unregisters the
   * connection, and delegates session cleanup to SessionManager.
   *
   * @param client_id       Disconnecting client identifier.
   * @param reason_code     DISCONNECT reason code.
   * @param expiry_override Optional Session Expiry Interval override.
   * @param now             Current timestamp.
   */
  void handle_disconnect(std::string_view client_id, ReasonCode reason_code,
                         std::optional<uint32_t> expiry_override,
                         std::chrono::steady_clock::time_point now);

  /**
   * @brief Thread-safe wrapper for abrupt connection loss (Module 17.3.3).
   *
   * Triggers will logic for connection loss, unregisters the connection,
   * and delegates session cleanup to SessionManager.
   *
   * @param client_id Client identifier.
   * @param now       Current timestamp.
   */
  void handle_connection_lost(std::string_view client_id,
                              std::chrono::steady_clock::time_point now);

  /**
   * @brief Thread-safe SUBSCRIBE facade (Module 19.1).
   *
   * For each requested filter, validates the topic filter, checks subscribe
   * ACL, stores authorised subscriptions, delivers matching retained messages,
   * and builds one reason code in the returned SUBACK.
   *
   * @param client_id Client identifier.
   * @param packet    Incoming SUBSCRIBE packet.
   * @return SUBACK packet with one reason code per filter.
   */
  [[nodiscard]] SubackPacket handle_subscribe(std::string_view client_id,
                                              const SubscribePacket &packet);

  /**
   * @brief Thread-safe UNSUBSCRIBE facade (Module 19.2).
   *
   * Removes each requested topic filter from the subscription store and builds
   * one UNSUBACK reason code per filter.
   *
   * @param client_id Client identifier.
   * @param packet    Incoming UNSUBSCRIBE packet.
   * @return UNSUBACK packet with one reason code per topic filter.
   */
  [[nodiscard]] UnsubackPacket
  handle_unsubscribe(std::string_view client_id,
                     const UnsubscribePacket &packet);

  /**
   * @brief Thread-safe PUBLISH facade (Module 19.3).
   *
   * Increments inbound statistics and routes the message through MessageRouter.
   *
   * @param msg         Message to route; may be modified in-place.
   * @param client_id   Publishing client identifier.
   * @param username    Username of the publishing client; may be empty.
   * @param alias_table Topic alias table for the publishing connection.
   */
  void handle_publish(Message &msg, std::string_view client_id,
                      std::string_view username, TopicAliasTable &alias_table);

  /**
   * @brief Advance the monitoring timer and publish `$SYS` stats if due
   *        (Module 16.2).
   *
   * Call from the main event loop.  Returns `true` when statistics were
   * published during this invocation.
   *
   * @param now Current time; defaults to `steady_clock::now()`.
   * @return `true` if `$SYS` topics were published.
   */
  bool tick(std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now());

  //  Connection registration

  /**
   * @brief Register an active connection with the broker (Module 20.2.1).
   *
   * Must be called after a client successfully completes the CONNECT
   * handshake.  The message router pushes outbound messages into the
   * client's @p queue.
   *
   * @param client_id  Client identifier.
   * @param queue      Shared outbound message queue for this client.
   */
  void register_connection(std::string_view client_id,
                           std::shared_ptr<OutboundQueue> queue);

  /**
   * @brief Unregister a connection (e.g. on disconnect or close).
   *
   * No-op when @p client_id is not registered.
   *
   * @param client_id Client identifier to remove.
   */
  void unregister_connection(std::string_view client_id) noexcept;

  //  Signal handling (15.3.3)

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
  /**
   * @brief Translate `AuthError` exceptions to MQTT reason codes.
   */
  [[nodiscard]] static ReasonCode
  map_auth_error_to_reason(AuthError error_code);

  /**
   * @brief Open or resume a session after successful authentication.
   */
  ConnectResult complete_connect_success(const ConnectPacket &connect_packet,
                                         std::function<void()> close_callback);

  /**
   * @brief Build an `AuthResult` representing a protocol error.
   */
  [[nodiscard]] static AuthResult protocol_error_result();

  /// Register connection when broker_mutex_ is already held exclusively.
  void register_connection_locked(std::string_view client_id,
                                  std::shared_ptr<OutboundQueue> queue);

  /// Unregister connection when broker_mutex_ is already held exclusively.
  void unregister_connection_locked(std::string_view client_id) noexcept;

  //  Internal helpers

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

  //  State

  BrokerConfig config_; ///< Broker configuration (copy from constructor).
  bool running_;        ///< True between startup() and shutdown().

  //  Persistence (Module 13)

  std::unique_ptr<SessionPersistence> session_persistence_;
  std::unique_ptr<RetainedMessagePersistence> retained_persistence_;
  std::unique_ptr<InflightPersistence> inflight_persistence_;

  //  In-memory stores (Module 4)

  std::unique_ptr<SessionStore> session_store_;
  std::unique_ptr<RetainedMessageStore> retained_store_;
  std::unique_ptr<SubscriptionStore> subscription_store_;
  std::unique_ptr<InflightStore> inflight_store_;

  //  Auth (Module 8)

  std::unique_ptr<AnonymousAuthenticator> anon_auth_;
  std::unique_ptr<PasswordAuthenticator> pass_auth_;
  IAuthenticator *active_auth_; ///< Non-owning pointer.

  struct PendingEnhancedAuthContext {
    EnhancedAuthHandler handler;
    ConnectPacket connect_packet;
    std::function<void()> close_callback;
  };

  std::unordered_map<std::string, PendingEnhancedAuthContext>
      pending_enhanced_auth_;
  std::unordered_map<std::string, EnhancedAuthHandler> active_enhanced_auth_;

  //  AuthZ (Module 9)

  std::unique_ptr<AclEngine> acl_engine_;
  std::unique_ptr<AclLoader> acl_loader_;

  //  Session Manager (Module 10)

  std::unique_ptr<SessionTakeoverHandler> takeover_handler_;
  std::unique_ptr<SessionExpiryScheduler> expiry_scheduler_;
  std::unique_ptr<SessionManager> session_manager_;

  //  Will Manager (Module 11)

  std::unique_ptr<WillStore> will_store_;
  std::unique_ptr<WillDelayTimer> will_delay_timer_;
  std::unique_ptr<WillPublisher> will_publisher_;

  //  Message Router (Module 12)

  std::unique_ptr<InboundPublishProcessor> publish_processor_;
  std::unique_ptr<OfflineQueue> offline_queue_;
  std::unique_ptr<SharedSubscriptionDispatcher> shared_dispatcher_;
  std::unique_ptr<MessageRouter> message_router_;

  //  Network (Module 6)

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;

  //  Connection tracking

  mutable std::shared_mutex
      broker_mutex_; ///< Guards shared mutable Broker state.
  std::unordered_map<std::string, std::shared_ptr<OutboundQueue>>
      active_connections_; ///< Online clients keyed by client ID (Module 20.2).

  //  Monitoring (Module 16)

  std::unique_ptr<StatisticsCollector> stats_collector_;
  std::unique_ptr<SysTopicPublisher> sys_publisher_;

  //  Accept threads (Module 17)

  std::vector<std::thread> accept_threads_; ///< One thread per open listener.

  //  Signal flag

  /// Set to `true` by the C signal handler on SIGTERM / SIGINT.
  static std::atomic<bool> shutdown_requested_;
};

} // namespace mqtt
