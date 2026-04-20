/**
 * @file broker.cpp
 * @brief Broker implementation — component wiring, startup/shutdown, and
 *        signal handling (Module 15.2 + 15.3).
 */

#include "broker/broker.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "broker/connack_properties.h"
#include "broker/broker_error.h"
#include "authz/broker_acl_policy.h"
#include "connection/client_handler.h"
#include "connection/outbound_queue_bridge.h"
#include "codec/packet/publish_codec.h"
#include "data_model/property/property_id.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/structured_tracer.h"
#include "monitoring/sys_topic_publisher.h"
#include "monitoring/trace_runtime_command.h"
#include "message_router/message_router_error.h"
#include "session_manager/session_manager_error.h"
#include "will_manager/will_message_util.h"

namespace mqtt {

namespace {

[[nodiscard]] std::string make_assigned_client_id() {
  static std::atomic<uint64_t> next_id{1U};
  const uint64_t sequence = next_id.fetch_add(1U, std::memory_order_relaxed);
  return std::format("auto-{}", sequence);
}

[[nodiscard]] ReasonCode
map_session_error_to_reason(SessionManagerError error_code) {
  switch (error_code) {
  case SessionManagerError::InvalidClientId:
    return ReasonCode::ClientIdentifierNotValid;
  }

  return ReasonCode::UnspecifiedError;
}

void append_authentication_method_property(std::vector<Property> &properties,
                                           std::string_view auth_method) {
  if (auth_method.empty()) {
    return;
  }

  properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                .value = Utf8String{std::string(auth_method)}});
}

[[nodiscard]] bool has_zero_topic_alias_property(const Message &message) {
  for (const Property &property : message.properties) {
    if (property.id != PropertyId::TopicAlias) {
      continue;
    }

    const auto *alias_value = std::get_if<uint16_t>(&property.value);
    if (alias_value != nullptr && *alias_value == 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::size_t estimated_publish_frame_bytes(const Message &message) {
  PublishPacket packet;
  packet.dup = false;
  packet.qos = message.qos;
  packet.retain = message.retain;
  packet.topic = message.topic;
  if (message.qos != QoS::AtMostOnce) {
    packet.packet_id = 1U;
  }
  packet.payload = message.payload;
  packet.properties = message.properties;

  WriteBuffer encoded_frame;
  encode_publish(encoded_frame, packet);
  return encoded_frame.size();
}

} // namespace

//
// Static member definition

std::atomic<bool> Broker::shutdown_requested_{false};

//
// Broker — construction / destruction

Broker::Broker(BrokerConfig config)
    : config_(std::move(config)), running_(false), active_auth_(nullptr) {}

Broker::~Broker() {
  if (running_) {
    shutdown();
  }
}

//
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

  connection_manager_->start();
  running_ = true;
}

void Broker::shutdown() noexcept {
  if (!running_) {
    return;
  }

  running_ = false;

  // Allow active client loops to observe shutdown and send DISCONNECT 0x8B
  // before transport teardown closes sockets.
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  std::vector<std::shared_ptr<OutboundQueue>> queues =
      connection_registry_->snapshot_queues();

  for (const auto &queue : queues) {
    if (queue) {
      queue->stop();
    }
  }

  if (connection_manager_) {
    connection_manager_->stop();
  }

  if (config_.persistence_enabled) {
    flush_persistence();
  }
}

bool Broker::is_running() const noexcept { return running_; }

//
// Module accessors

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
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);

  ConnectPacket effective_connect = connect_packet;
  std::optional<std::string> assigned_client_id;
  if (effective_connect.client_id.value.empty()) {
    assigned_client_id = make_assigned_client_id();
    effective_connect.client_id.value = *assigned_client_id;
  }

  ConnectResult result;
  result.client_id = effective_connect.client_id.value;
  result.connack_properties = build_static_connack_properties(config_);
  append_connect_driven_connack_properties(effective_connect,
                                           result.connack_properties);
  if (assigned_client_id.has_value()) {
    result.connack_properties.push_back(
        Property{.id = PropertyId::AssignedClientIdentifier,
                 .value = Utf8String{*assigned_client_id}});
  }

  auto return_with_trace = [this, &effective_connect](ConnectResult traced_result) {
    emit_connect_trace(effective_connect, traced_result);
    return traced_result;
  };

  pending_enhanced_auth_.erase(result.client_id);
  active_enhanced_auth_.erase(result.client_id);

  if (EnhancedAuthHandler::is_enhanced(effective_connect)) {
    EnhancedAuthHandler auth_handler(std::shared_ptr<IAuthenticator>(
        active_auth_, [](IAuthenticator * /*unused*/) {}));

    AuthResult auth_result{};
    try {
      auth_result = auth_handler.initiate(effective_connect);
    } catch (const AuthException &exception) {
      result.auth_status = AuthStatus::Failure;
      result.reason_code = map_auth_error_to_reason(exception.error());
      return return_with_trace(std::move(result));
    }

    result.auth_status = auth_result.status;
    result.reason_code = auth_result.reason_code;
    result.auth_data = auth_result.auth_data;
    result.auth_method = std::string(auth_handler.auth_method());

    if (auth_result.status == AuthStatus::Continue) {
      pending_enhanced_auth_.insert_or_assign(
          result.client_id, PendingEnhancedAuthContext{
                                .handler = std::move(auth_handler),
                                .connect_packet = effective_connect,
                                .close_callback = std::move(close_callback),
                                .assigned_client_id = assigned_client_id});
      return return_with_trace(std::move(result));
    }

    if (auth_result.status == AuthStatus::Failure) {
      return return_with_trace(std::move(result));
    }

    ConnectResult completed = complete_connect_success(effective_connect,
                                                       std::move(close_callback));
    completed.auth_status = AuthStatus::Success;
    completed.auth_method = std::string(auth_handler.auth_method());
    append_connect_driven_connack_properties(effective_connect,
                                             completed.connack_properties);
    append_authentication_method_property(completed.connack_properties,
                                          completed.auth_method);
    if (assigned_client_id.has_value()) {
      completed.connack_properties.push_back(
          Property{.id = PropertyId::AssignedClientIdentifier,
                   .value = Utf8String{*assigned_client_id}});
    }
    active_enhanced_auth_.insert_or_assign(completed.client_id,
                                           std::move(auth_handler));
    return return_with_trace(std::move(completed));
  }

  AuthResult auth_result{};
  try {
    auth_result = active_auth_->authenticate(effective_connect);
  } catch (const AuthException &exception) {
    result.auth_status = AuthStatus::Failure;
    result.reason_code = map_auth_error_to_reason(exception.error());
    return return_with_trace(std::move(result));
  }

  result.auth_status = auth_result.status;
  result.reason_code = auth_result.reason_code;
  result.auth_data = auth_result.auth_data;

  if (auth_result.status != AuthStatus::Success) {
    return return_with_trace(std::move(result));
  }

  ConnectResult completed =
      complete_connect_success(effective_connect, std::move(close_callback));
  completed.auth_status = AuthStatus::Success;
  append_connect_driven_connack_properties(effective_connect,
                                           completed.connack_properties);
  if (assigned_client_id.has_value()) {
    completed.connack_properties.push_back(
        Property{.id = PropertyId::AssignedClientIdentifier,
                 .value = Utf8String{*assigned_client_id}});
  }
  return return_with_trace(std::move(completed));
}

ConnectResult Broker::handle_auth_packet(std::string_view client_id,
                                         const AuthPacket &auth_packet) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);

  ConnectResult result;
  result.client_id = std::string(client_id);
  result.connack_properties = build_static_connack_properties(config_);

  auto pending_it = pending_enhanced_auth_.find(result.client_id);
  if (pending_it == pending_enhanced_auth_.end()) {
    const AuthResult protocol_error = protocol_error_result();
    result.auth_status = protocol_error.status;
    result.reason_code = protocol_error.reason_code;
    return result;
  }

  AuthResult auth_result{};
  try {
    auth_result = pending_it->second.handler.on_auth(auth_packet);
  } catch (const AuthException &exception) {
    result.auth_status = AuthStatus::Failure;
    result.reason_code = map_auth_error_to_reason(exception.error());
    pending_enhanced_auth_.erase(pending_it);
    return result;
  }

  result.auth_status = auth_result.status;
  result.reason_code = auth_result.reason_code;
  result.auth_data = auth_result.auth_data;
  result.auth_method = std::string(pending_it->second.handler.auth_method());

  if (auth_result.status == AuthStatus::Continue) {
    return result;
  }

  if (auth_result.status == AuthStatus::Failure) {
    pending_enhanced_auth_.erase(pending_it);
    return result;
  }

  ConnectResult completed =
      complete_connect_success(pending_it->second.connect_packet,
                               std::move(pending_it->second.close_callback));
  completed.auth_status = AuthStatus::Success;
  completed.auth_method = std::string(pending_it->second.handler.auth_method());
  append_connect_driven_connack_properties(pending_it->second.connect_packet,
                                           completed.connack_properties);
  append_authentication_method_property(completed.connack_properties,
                                        completed.auth_method);
  if (pending_it->second.assigned_client_id.has_value()) {
    completed.connack_properties.push_back(
        Property{.id = PropertyId::AssignedClientIdentifier,
                 .value = Utf8String{*pending_it->second.assigned_client_id}});
  }
  active_enhanced_auth_.insert_or_assign(completed.client_id,
                                         std::move(pending_it->second.handler));
  pending_enhanced_auth_.erase(pending_it);
  return completed;
}

AuthResult Broker::handle_reauthenticate(std::string_view client_id,
                                         const AuthPacket &auth_packet) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);

  auto active_it = active_enhanced_auth_.find(std::string(client_id));
  if (active_it == active_enhanced_auth_.end()) {
    return protocol_error_result();
  }

  AuthResult auth_result{};
  try {
    auth_result = active_it->second.reauthenticate(auth_packet);
  } catch (const AuthException &exception) {
    return {.status = AuthStatus::Failure,
            .reason_code = map_auth_error_to_reason(exception.error()),
            .auth_data = {}};
  }

  if (auth_result.status == AuthStatus::Failure) {
    active_enhanced_auth_.erase(active_it);
  }

  return auth_result;
}

ConnectResult
Broker::complete_connect_success(const ConnectPacket &connect_packet,
                                 std::function<void()> close_callback) {
  ConnectResult result;
  result.client_id = connect_packet.client_id.value;
  result.connack_properties = build_static_connack_properties(config_);

  try {
    const SessionOpenResult session_open = session_manager_->handle_connect(
        connect_packet, std::move(close_callback));
    result.session_present = session_open.session_present;
  } catch (const SessionManagerException &exception) {
    result.reason_code = map_session_error_to_reason(exception.error());
    result.auth_status = AuthStatus::Failure;
    return result;
  }

  if (connect_packet.will.has_value()) {
    will_publisher_->on_connect(
        result.client_id, will_data_to_will_message(*connect_packet.will));
  }

  result.auth_status = AuthStatus::Success;
  return result;
}

ReasonCode Broker::map_auth_error_to_reason(AuthError error_code) {
  switch (error_code) {
  case AuthError::NotAuthorized:
    return ReasonCode::NotAuthorized;
  case AuthError::BadMethod:
    return ReasonCode::BadAuthenticationMethod;
  case AuthError::ProtocolError:
    return ReasonCode::ProtocolError;
  case AuthError::InvalidState:
    return ReasonCode::ProtocolError;
  }

  return ReasonCode::UnspecifiedError;
}

AuthResult Broker::protocol_error_result() {
  return {.status = AuthStatus::Failure,
          .reason_code = ReasonCode::ProtocolError,
          .auth_data = {}};
}

void Broker::handle_disconnect(std::string_view client_id,
                               ReasonCode reason_code,
                               std::optional<uint32_t> expiry_override,
                               std::chrono::steady_clock::time_point now,
                               const std::shared_ptr<OutboundQueue>& connection_queue) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "disconnect_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("reason_code", std::to_string(static_cast<int>(reason_code)));
    event.data.emplace_back("expiry_override",
         expiry_override.has_value() ? std::to_string(*expiry_override)
                                     : "<unset>");
    structured_tracer_->emit(event);
  }
  pending_enhanced_auth_.erase(std::string(client_id));
  active_enhanced_auth_.erase(std::string(client_id));
  will_publisher_->on_disconnect(client_id, reason_code, now);
  unregister_connection_locked(client_id, connection_queue);
  session_manager_->handle_disconnect(client_id, expiry_override, now);
}

bool Broker::is_disconnect_expiry_override_valid(
    std::string_view client_id,
    std::optional<uint32_t> expiry_override) {
  std::shared_lock<std::shared_mutex> lock_guard(broker_mutex_);
  return session_manager_->is_disconnect_expiry_override_valid(client_id,
                                                               expiry_override);
}

void Broker::handle_connection_lost(std::string_view client_id,
                                    std::chrono::steady_clock::time_point now,
                                    const std::shared_ptr<OutboundQueue>& connection_queue) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connection_lost_handled";
    event.data.push_back({"client_id", std::string(client_id)});
    structured_tracer_->emit(event);
  }
  pending_enhanced_auth_.erase(std::string(client_id));
  active_enhanced_auth_.erase(std::string(client_id));
  will_publisher_->on_connection_lost(client_id, now);
  unregister_connection_locked(client_id, connection_queue);
  session_manager_->handle_disconnect(client_id, std::nullopt, now);
}

SubackPacket Broker::handle_subscribe(std::string_view client_id,
                                      const SubscribePacket &packet) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  SubackPacket suback =
      subscription_orchestrator_->handle_subscribe(client_id, packet);

  std::size_t failure_count = 0U;
  for (const ReasonCode reason_code : suback.reason_codes) {
    if (is_error(reason_code)) {
      ++failure_count;
    }
  }

  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "subscribe_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("requested_filters",
                            std::to_string(packet.filters.size()));
    event.data.emplace_back("granted_filters",
                            std::to_string(suback.reason_codes.size() - failure_count));
    event.data.emplace_back("failed_filters", std::to_string(failure_count));
    structured_tracer_->emit(event);
  }

  return suback;
}

UnsubackPacket Broker::handle_unsubscribe(std::string_view client_id,
                                          const UnsubscribePacket &packet) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  UnsubackPacket unsuback =
      subscription_orchestrator_->handle_unsubscribe(client_id, packet);

  std::size_t failure_count = 0U;
  for (const ReasonCode reason_code : unsuback.reason_codes) {
    if (is_error(reason_code)) {
      ++failure_count;
    }
  }

  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "unsubscribe_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("requested_filters",
                            std::to_string(packet.topic_filters.size()));
    event.data.emplace_back("successful_filters",
                            std::to_string(unsuback.reason_codes.size() - failure_count));
    event.data.emplace_back("failed_filters", std::to_string(failure_count));
    structured_tracer_->emit(event);
  }

  return unsuback;
}

ReasonCode Broker::handle_publish(Message &msg, std::string_view client_id,
                                  std::string_view username,
                                  TopicAliasTable &alias_table) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  stats_collector_->on_message_inbound();

  TRACE_GUARD(structured_tracer_, TraceLevel::Trace, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "broker";
    event.info = "publish_received";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("topic", msg.topic.value);
    event.data.emplace_back("qos", std::to_string(static_cast<int>(msg.qos)));
    event.data.emplace_back("payload_bytes",
                            std::to_string(msg.payload.data.size()));
    structured_tracer_->emit(event);
  }

  if (has_zero_topic_alias_property(msg)) {
    TRACE_GUARD(structured_tracer_, TraceLevel::Trace, "broker") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "broker";
      event.info = "publish_rejected_zero_topic_alias";
      event.data.emplace_back("client_id", std::string(client_id));
      event.data.emplace_back("topic", msg.topic.value);
      event.data.emplace_back("payload_bytes",
                              std::to_string(msg.payload.data.size()));
      structured_tracer_->emit(event);
    }
    return ReasonCode::ImplementationSpecificError;
  }

  try {
    const bool has_matching_subscribers =
        message_router_->route(msg, client_id, username, alias_table);
    TRACE_GUARD(structured_tracer_, TraceLevel::Trace, "broker") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "broker";
      event.info = "publish_routed";
      event.data.emplace_back("client_id", std::string(client_id));
      event.data.emplace_back("topic", msg.topic.value);
      event.data.emplace_back("payload_bytes",
              std::to_string(msg.payload.data.size()));
      event.data.emplace_back("has_matching_subscribers",
                              has_matching_subscribers ? "true" : "false");
      structured_tracer_->emit(event);
    }
    if (!has_matching_subscribers) {
      return ReasonCode::NoMatchingSubscribers;
    }
    return ReasonCode::Success;
  } catch (const MessageRouterException &exception) {
    if (exception.error() == MessageRouterError::PublishNotAuthorized) {
      return ReasonCode::NotAuthorized;
    }
    if (exception.error() == MessageRouterError::TopicAliasInvalid) {
      return ReasonCode::ProtocolError;
    }
    if (exception.error() == MessageRouterError::QueueFull) {
      return ReasonCode::QuotaExceeded;
    }

    TRACE_GUARD(structured_tracer_, TraceLevel::Error, "broker") {
      TraceEvent event;
      event.level = TraceLevel::Error;
      event.module = "broker";
      event.info = "publish_route_failed";
      event.data.emplace_back("client_id", std::string(client_id));
      event.data.emplace_back("topic", msg.topic.value);
      event.data.emplace_back("payload_bytes",
              std::to_string(msg.payload.data.size()));
      event.data.emplace_back("router_error",
                              std::to_string(static_cast<int>(exception.error())));
      structured_tracer_->emit(event);
    }
    throw;
  }
}

//
// Connection registration

void Broker::register_connection(std::string_view client_id,
                                 std::shared_ptr<OutboundQueue> queue) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  register_connection_locked(client_id, std::move(queue));
}

void Broker::unregister_connection(std::string_view client_id) noexcept {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  unregister_connection_locked(client_id, nullptr);
}

//
// Monitoring (Module 16)

bool Broker::tick(std::chrono::steady_clock::time_point now) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  will_publisher_->publish_due(now);

  const std::vector<std::string> expired_sessions =
      session_manager_->cleanup_expired(now);
  for (const std::string &client_id : expired_sessions) {
    will_publisher_->on_session_expired(client_id);
  }

  return sys_publisher_->tick(now);
}

void Broker::apply_trace_system_message(const Message &message) {
  std::unique_lock<std::shared_mutex> lock_guard(broker_mutex_);
  if (!structured_tracer_) {
    return;
  }

  apply_trace_runtime_command(*structured_tracer_, message);
}

void Broker::emit_connect_trace(const ConnectPacket &connect_packet,
                                const ConnectResult &connect_result) noexcept {
  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connect_handled";
    event.data.emplace_back(
        "client_id",
        connect_result.client_id.empty() ? "<empty>" : connect_result.client_id);
    event.data.emplace_back("clean_start",
                            connect_packet.clean_start ? "true" : "false");
    event.data.emplace_back(
        "auth_status",
        std::to_string(static_cast<int>(connect_result.auth_status)));
    event.data.emplace_back(
        "reason_code",
        std::to_string(static_cast<int>(connect_result.reason_code)));

    structured_tracer_->emit(event);
  }
}

void Broker::register_connection_locked(std::string_view client_id,
                                        std::shared_ptr<OutboundQueue> queue) {
  const ConnectionUpsertResult upsert_result =
      connection_registry_->upsert(client_id, queue);
  if (upsert_result.replaced_existing && upsert_result.previous_queue && queue &&
      upsert_result.previous_queue != queue) {
    (void)transfer_pending_outbound_messages(*upsert_result.previous_queue,
                                             *queue);
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

void Broker::unregister_connection_locked(
    std::string_view client_id,
    const std::shared_ptr<OutboundQueue> &expected_queue) noexcept {
  const ConnectionRemoveResult remove_result =
      connection_registry_->remove_if_matches(client_id, expected_queue);
  if (!remove_result.removed) {
    return;
  }

  std::size_t moved_to_offline = 0U;
  if (message_router_ && remove_result.removed_queue) {
    std::vector<Message> pending_messages =
        drain_pending_outbound_messages(*remove_result.removed_queue);
    moved_to_offline =
        message_router_->buffer_offline_messages(client_id,
                                                 std::move(pending_messages));
  }

  TRACE_GUARD(structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connection_unregistered";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("moved_to_offline", std::to_string(moved_to_offline));
    event.data.emplace_back("active_connections_before",
                            std::to_string(remove_result.active_connections_before));
    structured_tracer_->emit(event);
  }

  if (shared_dispatcher_) {
    shared_dispatcher_->remove_client(client_id);
  }
  const std::size_t erase_count = 1U;
  if (erase_count > 0U) {
    stats_collector_->on_client_disconnected();
  }
}

//
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

//
// Private — create_modules (15.2.1)

void Broker::create_modules() {
  //  Persistence adapters (Module 13)
  session_persistence_ =
      std::make_unique<SessionPersistence>(config_.persistence_dir);
  retained_persistence_ =
      std::make_unique<RetainedMessagePersistence>(config_.persistence_dir);
  inflight_persistence_ =
      std::make_unique<InflightPersistence>(config_.persistence_dir);

  //  In-memory stores (Module 4)
  session_store_ = std::make_unique<SessionStore>();
  retained_store_ = std::make_unique<RetainedMessageStore>();
  subscription_store_ = std::make_unique<SubscriptionStore>();
  inflight_store_ = std::make_unique<InflightStore>();

  //  Auth (Module 8)
  // 15.2.3: bind auth module according to config
  if (config_.allow_anonymous) {
    anon_auth_ =
        std::make_unique<AnonymousAuthenticator>(AnonymousPolicy::Allow);
    active_auth_ = anon_auth_.get();
  } else {
    pass_auth_ = std::make_unique<PasswordAuthenticator>();
    for (const PasswordCredentialConfig &credential :
         config_.password_credentials) {
      pass_auth_->add_credential(Utf8String{credential.username},
                                 binary_data_from_string(credential.password));
    }
    active_auth_ = pass_auth_.get();
  }

  //  AuthZ (Module 9)
  acl_engine_ = std::make_unique<AclEngine>();
  acl_loader_ = std::make_unique<AclLoader>(*acl_engine_);

  // Base ACL rules:
  // 1. Broker-internal publish principal allow-all.
  // 2. Configured ACL rules in configured order.
  // 3. Optional anonymous fallback allow-all.
  std::vector<AclRuleConfig> acl_rules =
      make_startup_acl_rules(config_.acl_rules, config_.allow_anonymous);
  acl_loader_->load(acl_rules);

  //  Session Manager (Module 10)
  takeover_handler_ = std::make_unique<SessionTakeoverHandler>();
  expiry_scheduler_ = std::make_unique<SessionExpiryScheduler>();
  session_manager_ = std::make_unique<SessionManager>(
      *session_store_, *subscription_store_, *inflight_store_,
      *takeover_handler_, *expiry_scheduler_);

  //  Message Router (Module 12)
  publish_processor_ = std::make_unique<InboundPublishProcessor>(
      *acl_engine_, *retained_store_, *subscription_store_);

  offline_queue_ = std::make_unique<OfflineQueue>(
      static_cast<std::size_t>(config_.max_queued_messages));
  connection_registry_ = std::make_unique<ActiveConnectionRegistry>();

  shared_dispatcher_ = std::make_unique<SharedSubscriptionDispatcher>();

  // Delivery callbacks: push into client's OutboundQueue (Module 20.2).
  auto is_online_fn = [this](std::string_view cid) -> bool {
    return connection_registry_->contains(cid);
  };
  auto deliver_fn = [this](std::string_view cid, const Message &msg) {
    std::shared_ptr<OutboundQueue> queue = connection_registry_->find(cid);
    if (queue) {
      const std::size_t frame_bytes = estimated_publish_frame_bytes(msg);
      if (frame_bytes > config_.write_queue_max_bytes) {
        TRACE_GUARD(structured_tracer_, TraceLevel::Warning, "broker") {
          TraceEvent event;
          event.level = TraceLevel::Warning;
          event.module = "broker";
          event.info = "outbound_frame_exceeds_write_queue_capacity";
          event.data.emplace_back("client_id", std::string(cid));
          event.data.emplace_back("topic", msg.topic.value);
          event.data.emplace_back("qos", std::to_string(static_cast<int>(msg.qos)));
          event.data.emplace_back("payload_bytes",
                                  std::to_string(msg.payload.data.size()));
          event.data.emplace_back("frame_bytes", std::to_string(frame_bytes));
          event.data.emplace_back("write_queue_max_bytes",
                                  std::to_string(config_.write_queue_max_bytes));
          structured_tracer_->emit(event);
        }
        throw MessageRouterException(
            MessageRouterError::QueueFull,
            "outbound frame exceeds write queue capacity");
      }

      stats_collector_->on_message_outbound();
      const bool pushed = queue->push(msg);
      const std::size_t queue_size = queue->size();
      TRACE_GUARD(structured_tracer_, TraceLevel::Trace, "broker") {
        TraceEvent event;
        event.level = pushed ? TraceLevel::Trace : TraceLevel::Warning;
        event.module = "broker";
        event.info = pushed ? "outbound_queue_push_ok" : "outbound_queue_push_dropped";
        event.data.emplace_back("client_id", std::string(cid));
        event.data.emplace_back("topic", msg.topic.value);
        event.data.emplace_back("qos", std::to_string(static_cast<int>(msg.qos)));
        event.data.emplace_back("payload_bytes",
                                std::to_string(msg.payload.data.size()));
        event.data.emplace_back("queue_size", std::to_string(queue_size));
        event.data.emplace_back("max_queued_messages",
                                std::to_string(config_.max_queued_messages));
        event.data.emplace_back("active_connections",
                                std::to_string(connection_registry_->size()));
        structured_tracer_->emit(event);
      }

      if (pushed && queue_size >= static_cast<std::size_t>(config_.max_queued_messages)) {
        TRACE_GUARD(structured_tracer_, TraceLevel::Warning, "broker") {
          TraceEvent event;
          event.level = TraceLevel::Warning;
          event.module = "broker";
          event.info = "outbound_queue_at_capacity";
          event.data.emplace_back("client_id", std::string(cid));
          event.data.emplace_back("topic", msg.topic.value);
          event.data.emplace_back("queue_size", std::to_string(queue_size));
          event.data.emplace_back("max_queued_messages",
                                  std::to_string(config_.max_queued_messages));
          structured_tracer_->emit(event);
        }
      }

      if (!pushed) {
        throw MessageRouterException(
            MessageRouterError::QueueFull,
            "online outbound queue capacity exceeded");
      }
    }
  };

  message_router_ = std::make_unique<MessageRouter>(
      *publish_processor_, *offline_queue_, *shared_dispatcher_,
      std::move(is_online_fn), std::move(deliver_fn));

  subscription_orchestrator_ = std::make_unique<SubscriptionOrchestrator>(
      *acl_engine_, *session_store_, *subscription_store_,
      *shared_dispatcher_, *message_router_);

  //  Will Manager (Module 11)
  will_store_ = std::make_unique<WillStore>();
  will_delay_timer_ = std::make_unique<WillDelayTimer>();

  // Will publish callback: route via MessageRouter using the reserved
  // broker-internal client ID that the ACL allows to publish everywhere.
  auto will_publish_fn = [this](const WillMessage &will) {
    // Build a mutable copy for route() which may modify topic in-place
    Message msg = will.message;
    message_router_->route_internal(msg, k_broker_internal_principal);
  };

  will_publisher_ = std::make_unique<WillPublisher>(
      *will_store_, *will_delay_timer_, std::move(will_publish_fn));

  //  Monitoring (Module 16)
  stats_collector_ = std::make_unique<StatisticsCollector>(*subscription_store_,
                                                           *retained_store_);

  //  Structured tracing (Module 26)
  structured_tracer_ = std::make_unique<StructuredTracer>(std::clog);
  structured_tracer_->set_global_level(config_.trace_global_level);
  structured_tracer_->set_trace_modules(config_.trace_modules);
  message_router_->set_tracer(structured_tracer_.get());
  session_manager_->set_tracer(structured_tracer_.get());

  // $SYS publish callback: route via MessageRouter using the reserved
  // broker-internal client ID that the ACL allows to publish everywhere.
  auto sys_publish_fn = [this](Message msg) {
    message_router_->route_internal(std::move(msg), k_broker_internal_principal);
  };

  sys_publisher_ = std::make_unique<SysTopicPublisher>(
      *stats_collector_, std::chrono::seconds(config_.sys_topic_interval),
      std::move(sys_publish_fn));

  auto client_handler_callback = [this](std::unique_ptr<TcpConnection> connection,
                                        bool is_ws) {
    ClientHandler handler;
    handler.run(std::move(connection), *this, config_, is_ws);
  };

  connection_manager_ = std::make_unique<ConnectionManager>(
      config_.mqtt_port, config_.ws_port, std::move(client_handler_callback));
}

//
// Private — persistence (15.2.2)

void Broker::load_persistence() {
  // Load sessions
  auto sessions = session_persistence_->load_all();
  for (const auto &sess : sessions) {
    session_store_->create(sess);
    for (const auto &subscription : sess.subscriptions) {
      subscription_store_->store(sess.client_id.value, subscription);
    }
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
        inflight_entries.push_back({.client_id=sess.client_id.value, .entry=entry});
      }
    }
    inflight_persistence_->save_all(inflight_entries);
  } catch (...) {
    // noexcept — swallow persistence errors during shutdown
  }
}

} // namespace mqtt
