/**
 * @file broker.cpp
 * @brief Broker implementation — component wiring, startup/shutdown, and
 *        signal handling (Module 15.2 + 15.3).
 */

#include "broker/broker.h"

#include <csignal>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include "broker/broker_error.h"
#include "connection/client_handler.h"
#include "connection/topic_alias_table.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/sys_topic_publisher.h"

namespace mqtt {

// ─────────────────────────────────────────────────────────────────────────────
// Static member definition

std::atomic<bool> Broker::shutdown_requested_{false};

// ─────────────────────────────────────────────────────────────────────────────
// Broker — construction / destruction

Broker::Broker(BrokerConfig config)
    : config_(std::move(config)), running_(false), active_auth_(nullptr) {}

Broker::~Broker() {
  if (running_) {
    shutdown();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle

void Broker::startup() {
  if (running_) {
    throw BrokerException(BrokerError::AlreadyRunning,
                          "Broker is already running");
  }

  create_modules();

  if (config_.persistence_enabled) {
    load_persistence();
  }

  open_listeners();
  running_ = true;
}

void Broker::shutdown() noexcept {
  if (!running_) {
    return;
  }

  close_listeners();

  if (config_.persistence_enabled) {
    flush_persistence();
  }

  running_ = false;
}

bool Broker::is_running() const noexcept { return running_; }

// ─────────────────────────────────────────────────────────────────────────────
// Module accessors

SessionManager &Broker::session_manager() noexcept { return *session_manager_; }

MessageRouter &Broker::message_router() noexcept { return *message_router_; }

IAuthenticator &Broker::authenticator() noexcept { return *active_auth_; }

AclEngine &Broker::acl_engine() noexcept { return *acl_engine_; }

WillPublisher &Broker::will_publisher() noexcept { return *will_publisher_; }

StatisticsCollector &Broker::statistics_collector() noexcept {
  return *stats_collector_;
}

SessionOpenResult Broker::handle_connect(const ConnectPacket &connect_packet,
                                         std::function<void()> close_callback) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  return session_manager_->handle_connect(connect_packet,
                                          std::move(close_callback));
}

void Broker::handle_disconnect(std::string_view client_id,
                               ReasonCode reason_code,
                               std::optional<uint32_t> expiry_override,
                               std::chrono::steady_clock::time_point now) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  will_publisher_->on_disconnect(client_id, reason_code, now);
  unregister_connection_locked(client_id);
  session_manager_->handle_disconnect(client_id, expiry_override, now);
}

void Broker::handle_connection_lost(std::string_view client_id,
                                    std::chrono::steady_clock::time_point now) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  will_publisher_->on_connection_lost(client_id, now);
  unregister_connection_locked(client_id);
  session_manager_->handle_disconnect(client_id, std::nullopt, now);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection registration

void Broker::register_connection(std::string_view client_id, SendFn send_fn) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  register_connection_locked(client_id, std::move(send_fn));
}

void Broker::unregister_connection(std::string_view client_id) noexcept {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  unregister_connection_locked(client_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// Monitoring (Module 16)

void Broker::route_message(Message &msg, std::string_view client_id,
                           std::string_view username,
                           TopicAliasTable &alias_table) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  stats_collector_->on_message_inbound();
  message_router_->route(msg, client_id, username, alias_table);
}

bool Broker::tick(std::chrono::steady_clock::time_point now) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  return sys_publisher_->tick(now);
}

void Broker::register_connection_locked(std::string_view client_id,
                                        SendFn send_fn) {
  const std::string key(client_id);
  const bool existed = active_connections_.contains(key);
  active_connections_[key] = std::move(send_fn);
  if (!existed) {
    stats_collector_->on_client_connected();
  }
}

void Broker::unregister_connection_locked(std::string_view client_id) noexcept {
  const std::size_t erase_count =
      active_connections_.erase(std::string(client_id));
  if (erase_count > 0U) {
    stats_collector_->on_client_disconnected();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling (15.3.3)

void Broker::install_signal_handlers() noexcept {
  shutdown_requested_.store(false);
  std::signal(SIGTERM, Broker::handle_signal);
  std::signal(SIGINT, Broker::handle_signal);
}

bool Broker::shutdown_requested() noexcept {
  return shutdown_requested_.load();
}

void Broker::handle_signal(int /*sig*/) noexcept {
  shutdown_requested_.store(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — create_modules (15.2.1)

void Broker::create_modules() {
  // ── Persistence adapters (Module 13) ─────────────────────────────────────
  session_persistence_ =
      std::make_unique<SessionPersistence>(config_.persistence_dir);
  retained_persistence_ =
      std::make_unique<RetainedMessagePersistence>(config_.persistence_dir);
  inflight_persistence_ =
      std::make_unique<InflightPersistence>(config_.persistence_dir);

  // ── In-memory stores (Module 4) ──────────────────────────────────────────
  session_store_ = std::make_unique<SessionStore>();
  retained_store_ = std::make_unique<RetainedMessageStore>();
  subscription_store_ = std::make_unique<SubscriptionStore>();
  inflight_store_ = std::make_unique<InflightStore>();

  // ── Auth (Module 8) ──────────────────────────────────────────────────────
  // 15.2.3: bind auth module according to config
  if (config_.allow_anonymous) {
    anon_auth_ =
        std::make_unique<AnonymousAuthenticator>(AnonymousPolicy::Allow);
    active_auth_ = anon_auth_.get();
  } else {
    pass_auth_ = std::make_unique<PasswordAuthenticator>();
    active_auth_ = pass_auth_.get();
  }

  // ── AuthZ (Module 9) ─────────────────────────────────────────────────────
  acl_engine_ = std::make_unique<AclEngine>();
  acl_loader_ = std::make_unique<AclLoader>(*acl_engine_);

  // Base ACL rules:
  // 1. Broker-internal will-message principal may publish/subscribe anywhere.
  // 2. When anonymous access is enabled, all clients may publish/subscribe.
  std::vector<AclRuleConfig> acl_rules;
  acl_rules.push_back(
      {"_broker_will_system_", "#", "publish_and_subscribe", "allow"});
  if (config_.allow_anonymous) {
    acl_rules.push_back({"*", "#", "publish_and_subscribe", "allow"});
  }
  acl_loader_->load(acl_rules);

  // ── Session Manager (Module 10) ──────────────────────────────────────────
  takeover_handler_ = std::make_unique<SessionTakeoverHandler>();
  expiry_scheduler_ = std::make_unique<SessionExpiryScheduler>();
  session_manager_ = std::make_unique<SessionManager>(
      *session_store_, *subscription_store_, *inflight_store_,
      *takeover_handler_, *expiry_scheduler_);

  // ── Message Router (Module 12) ───────────────────────────────────────────
  publish_processor_ = std::make_unique<InboundPublishProcessor>(
      *acl_engine_, *retained_store_, *subscription_store_);

  offline_queue_ = std::make_unique<OfflineQueue>(
      static_cast<std::size_t>(config_.max_queued_messages));

  shared_dispatcher_ = std::make_unique<SharedSubscriptionDispatcher>();

  // Delivery callbacks: look up in the active_connections_ map.
  auto is_online_fn = [this](std::string_view cid) -> bool {
    return active_connections_.contains(std::string(cid));
  };
  auto deliver_fn = [this](std::string_view cid, const Message &msg) {
    auto iter = active_connections_.find(std::string(cid));
    if (iter != active_connections_.end()) {
      stats_collector_->on_message_outbound();
      iter->second(msg);
    }
  };

  message_router_ = std::make_unique<MessageRouter>(
      *publish_processor_, *offline_queue_, *shared_dispatcher_,
      std::move(is_online_fn), std::move(deliver_fn));

  // ── Will Manager (Module 11) ─────────────────────────────────────────────
  will_store_ = std::make_unique<WillStore>();
  will_delay_timer_ = std::make_unique<WillDelayTimer>();

  // Will publish callback: route via MessageRouter using the reserved
  // broker-internal client ID that the ACL allows to publish everywhere.
  auto will_publish_fn = [this](const WillMessage &will) {
    // Build a mutable copy for route() which may modify topic in-place
    Message msg = will.message;
    // A will publish is server-initiated — no topic aliases apply.
    // Use a local empty alias table.
    TopicAliasTable alias_table(0U);
    message_router_->route(msg, "_broker_will_system_", "", alias_table);
  };

  will_publisher_ = std::make_unique<WillPublisher>(
      *will_store_, *will_delay_timer_, std::move(will_publish_fn));

  // ── Monitoring (Module 16) ────────────────────────────────────────────
  stats_collector_ = std::make_unique<StatisticsCollector>(*subscription_store_,
                                                           *retained_store_);

  // $SYS publish callback: route via MessageRouter using the reserved
  // broker-internal client ID that the ACL allows to publish everywhere.
  auto sys_publish_fn = [this](Message msg) {
    TopicAliasTable alias_table(0U);
    message_router_->route(msg, "_broker_will_system_", "", alias_table);
  };

  sys_publisher_ = std::make_unique<SysTopicPublisher>(
      *stats_collector_, std::chrono::seconds(config_.sys_topic_interval),
      std::move(sys_publish_fn));
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — persistence (15.2.2)

void Broker::load_persistence() {
  // Load sessions
  auto sessions = session_persistence_->load_all();
  for (const auto &sess : sessions) {
    session_store_->create(sess);
  }

  // Load retained messages
  auto retained = retained_persistence_->load_all();
  for (const auto &msg : retained) {
    retained_store_->store(msg);
  }

  // Load inflight entries
  auto entries = inflight_persistence_->load_all();
  for (const auto &entry : entries) {
    inflight_store_->create(entry.client_id, entry.entry);
  }
}

void Broker::flush_persistence() noexcept {
  try {
    // Sessions
    std::vector<SessionState> sessions = session_store_->all();
    session_persistence_->save_all(sessions);

    // Retained messages
    std::vector<Message> retained = retained_store_->all();
    retained_persistence_->save_all(retained);

    // Inflight entries
    std::vector<InflightPersistence::ClientEntry> inflight_entries;
    for (const auto &sess : sessions) {
      auto client_entries = inflight_store_->entries_for(sess.client_id.value);
      for (const auto &entry : client_entries) {
        inflight_entries.push_back({sess.client_id.value, entry});
      }
    }
    inflight_persistence_->save_all(inflight_entries);
  } catch (...) {
    // noexcept — swallow persistence errors during shutdown
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — listeners (15.3.1 / 15.3.2)

void Broker::open_listeners() {
  auto spawn_accept_thread = [this](TcpListener &listener, bool is_ws) {
    accept_threads_.emplace_back([this, &listener, is_ws]() {
      while (!shutdown_requested_.load()) {
        try {
          auto conn = listener.accept();
          if (!conn) {
            continue;
          }
          std::thread([this, connection = std::move(conn), is_ws]() mutable {
            ClientHandler handler;
            handler.run(std::move(connection), *this, config_, is_ws);
          }).detach();
        } catch (...) {
          // accept() throws when the listener is closed during shutdown
          break;
        }
      }
    });
  };

  if (config_.mqtt_port != 0U) {
    mqtt_listener_ = TcpListener::listen(config_.mqtt_port);
    spawn_accept_thread(*mqtt_listener_, false);
  }
  if (config_.ws_port != 0U) {
    ws_listener_ = TcpListener::listen(config_.ws_port);
    spawn_accept_thread(*ws_listener_, true);
  }
}

void Broker::close_listeners() noexcept {
  if (mqtt_listener_.has_value()) {
    mqtt_listener_->close();
    mqtt_listener_.reset();
  }
  if (ws_listener_.has_value()) {
    ws_listener_->close();
    ws_listener_.reset();
  }
  for (auto &thr : accept_threads_) {
    if (thr.joinable()) {
      thr.join();
    }
  }
  accept_threads_.clear();
}

} // namespace mqtt
