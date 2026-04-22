/**
 * @file broker.cpp
 * @brief Broker implementation — startup/shutdown, signal handling, and
 *        delegation to extracted facades.
 */

#include "broker/broker.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "broker/broker_error.h"
#include "broker/broker_module_factory.h"
#include "broker/connect_facade.h"
#include "broker/disconnect_facade.h"
#include "broker/enhanced_auth_registry.h"
#include "broker/persistence_coordinator.h"
#include "broker/publish_facade.h"
#include "broker/subscribe_facade.h"
#include "broker/tick_handler.h"
#include "connection/client_handler.h"
#include "connection/outbound_queue_bridge.h"

namespace mqtt {

std::atomic<bool> Broker::shutdown_requested_{false};

Broker::Broker(BrokerConfig config)
    : config_(std::move(config)), running_(false), active_auth_(nullptr) {}

Broker::~Broker() {
  if (running_) {
    shutdown();
  }
}

void Broker::startup() {
  if (running_) {
    throw BrokerException(BrokerError::AlreadyRunning,
                          "Broker is already running");
  }

  create_modules();

  if (config_.persistence_mode != PersistenceMode::Off) {
    const bool include_inflight_states =
        (config_.persistence_mode == PersistenceMode::Full);
    PersistenceCoordinator::load(*session_persistence_, *retained_persistence_,
                                 *inflight_persistence_,
                                 *offline_queue_persistence_,
                                 include_inflight_states, *session_store_,
                                 *retained_store_, *subscription_store_,
                                 *inflight_store_, *offline_queue_);
  }

  connection_manager_->start();
  running_ = true;
}

void Broker::shutdown() noexcept {
  if (!running_) {
    return;
  }

  running_ = false;
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const std::vector<std::shared_ptr<OutboundQueue>> queues =
      connection_registry_->snapshot_queues();
  for (const auto &queue : queues) {
    if (queue) {
      queue->stop();
    }
  }

  if (connection_manager_) {
    connection_manager_->stop();
  }

  if (config_.persistence_mode != PersistenceMode::Off) {
    const bool include_inflight_states =
        (config_.persistence_mode == PersistenceMode::Full);
    PersistenceCoordinator::flush(*session_persistence_, *retained_persistence_,
                                  *inflight_persistence_,
                                  *offline_queue_persistence_,
                                  include_inflight_states, *session_store_,
                                  *retained_store_, *inflight_store_,
                                  *offline_queue_);
  }
}

bool Broker::is_running() const noexcept { return running_; }

SessionManager &Broker::session_manager() noexcept { return *session_manager_; }

MessageRouter &Broker::message_router() noexcept { return *message_router_; }

IAuthenticator &Broker::authenticator() noexcept { return *active_auth_; }

AclEngine &Broker::acl_engine() noexcept { return *acl_engine_; }

WillPublisher &Broker::will_publisher() noexcept { return *will_publisher_; }

StatisticsCollector &Broker::statistics_collector() noexcept {
  return *stats_collector_;
}

StructuredTracer &Broker::structured_tracer() noexcept {
  return *structured_tracer_;
}

ConnectResult Broker::handle_connect(const ConnectPacket &connect_packet,
                                     std::function<void()> close_callback) {
  return connect_facade_->handle_connect(connect_packet, std::move(close_callback));
}

ConnectResult Broker::handle_auth_packet(std::string_view client_id,
                                         const AuthPacket &auth_packet) {
  return connect_facade_->handle_auth_packet(client_id, auth_packet);
}

AuthResult Broker::handle_reauthenticate(std::string_view client_id,
                                         const AuthPacket &auth_packet) {
  return connect_facade_->handle_reauthenticate(client_id, auth_packet);
}

void Broker::handle_disconnect(
    std::string_view client_id, ReasonCode reason_code,
    std::optional<uint32_t> expiry_override,
    std::chrono::steady_clock::time_point now,
    const std::shared_ptr<OutboundQueue> &connection_queue) {
  disconnect_facade_->handle_disconnect(client_id, reason_code, expiry_override, now,
                                        connection_queue);
}

bool Broker::is_disconnect_expiry_override_valid(
    std::string_view client_id, std::optional<uint32_t> expiry_override) {
  return disconnect_facade_->is_disconnect_expiry_override_valid(client_id,
                                                                 expiry_override);
}

void Broker::handle_connection_lost(
    std::string_view client_id, std::chrono::steady_clock::time_point now,
    const std::shared_ptr<OutboundQueue> &connection_queue) {
  disconnect_facade_->handle_connection_lost(client_id, now, connection_queue);
}

SubackPacket Broker::handle_subscribe(std::string_view client_id,
                                      const SubscribePacket &packet) {
  return subscribe_facade_->handle_subscribe(client_id, packet);
}

UnsubackPacket Broker::handle_unsubscribe(std::string_view client_id,
                                          const UnsubscribePacket &packet) {
  return subscribe_facade_->handle_unsubscribe(client_id, packet);
}

ReasonCode Broker::handle_publish(Message &msg, std::string_view client_id,
                                  std::string_view username,
                                  TopicAliasTable &alias_table) {
  return publish_facade_->handle_publish(msg, client_id, username, alias_table);
}

void Broker::register_connection(std::string_view client_id,
                                 std::shared_ptr<OutboundQueue> queue) {
  const ConnectionUpsertResult upsert_result =
      connection_registry_->upsert(client_id, queue);
  if (upsert_result.replaced_existing && upsert_result.previous_queue && queue &&
      upsert_result.previous_queue != queue) {
    (void)transfer_pending_outbound_messages(*upsert_result.previous_queue, *queue);
  }

  if (!upsert_result.replaced_existing) {
    stats_collector_->on_client_connected();
  }

  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connection_registered";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("replaced_existing",
                            upsert_result.replaced_existing ? "true" : "false");
    event.data.emplace_back("active_connections",
                            std::to_string(upsert_result.active_connections));
    structured_tracer_->emit(event);
  }
}

void Broker::unregister_connection(std::string_view client_id) noexcept {
  disconnect_facade_->unregister_connection(client_id, nullptr);
}

bool Broker::tick(std::chrono::steady_clock::time_point now) {
  return tick_handler_->tick(now);
}

void Broker::apply_trace_system_message(const Message &message) {
  tick_handler_->apply_trace_system_message(message);
}

void Broker::install_signal_handlers() noexcept {
  shutdown_requested_.store(false);
  std::signal(SIGTERM, Broker::handle_signal);
  std::signal(SIGINT, Broker::handle_signal);
}

bool Broker::shutdown_requested() noexcept { return shutdown_requested_.load(); }

void Broker::handle_signal(int /*sig*/) noexcept { shutdown_requested_.store(true); }

void Broker::create_modules() {
  auto client_handler_callback = [this](std::unique_ptr<TcpConnection> connection,
                                        bool is_ws) {
    ClientHandler handler;
    handler.run(std::move(connection), *this, config_, is_ws);
  };

  BrokerModuleFactory::create(
      config_, session_persistence_, retained_persistence_, inflight_persistence_,
      offline_queue_persistence_, session_store_, retained_store_,
      subscription_store_, inflight_store_, anon_auth_, pass_auth_, active_auth_,
      acl_engine_, acl_loader_, takeover_handler_, expiry_scheduler_,
      session_manager_, publish_processor_, offline_queue_, shared_dispatcher_,
      subscription_orchestrator_, message_router_, connection_registry_,
      will_store_, will_delay_timer_, will_publisher_, stats_collector_,
      structured_tracer_, sys_publisher_, connection_manager_,
      std::move(client_handler_callback));

  if (connection_manager_) {
    connection_manager_->bind_runtime(*this, config_);
  }

  enhanced_auth_registry_ = std::make_unique<EnhancedAuthRegistry>();
  connect_facade_ = std::make_unique<ConnectFacade>(
      config_, *active_auth_, *session_manager_, *will_publisher_,
      *enhanced_auth_registry_, *structured_tracer_);
  disconnect_facade_ = std::make_unique<DisconnectFacade>(
      *will_publisher_, *session_manager_, *enhanced_auth_registry_,
      *connection_registry_, *message_router_, *shared_dispatcher_,
      *stats_collector_, *structured_tracer_);
  publish_facade_ = std::make_unique<PublishFacade>(*message_router_,
                                                     *stats_collector_,
                                                     *structured_tracer_);
  subscribe_facade_ =
      std::make_unique<SubscribeFacade>(*subscription_orchestrator_,
                                        *structured_tracer_);
  tick_handler_ = std::make_unique<TickHandler>(*will_publisher_,
                                                *session_manager_, *sys_publisher_,
                                                *structured_tracer_);

  if (config_.persistence_mode != PersistenceMode::Off) {
    publish_processor_->set_on_retained_changed(
        [this]() noexcept {
          try {
            retained_persistence_->save_all(retained_store_->all());
          } catch (...) {}
        });

    auto save_sessions = [this]() noexcept {
      try {
        session_persistence_->save_all(session_store_->all());
      } catch (...) {}
    };
    session_manager_->set_on_session_changed(save_sessions);
    subscription_orchestrator_->set_on_session_changed(save_sessions);

    message_router_->set_on_offline_queue_changed(
        [this]() noexcept {
          try {
            const auto snap = offline_queue_->snapshot();
            std::vector<OfflineQueuePersistence::ClientMessages> entries;
            entries.reserve(snap.size());
            for (const auto &[cid, msgs] : snap) {
              entries.push_back({.client_id = cid, .messages = msgs});
            }
            offline_queue_persistence_->save_all(entries);
          } catch (...) {}
        });
  }
}

} // namespace mqtt
