#include "broker/connect_facade.h"

#include <atomic>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "auth/auth_error.h"
#include "broker/connack_properties.h"
#include "data_model/property/property_id.h"
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

} // namespace

ConnectFacade::ConnectFacade(const BrokerConfig &config, IAuthenticator &active_auth,
                             SessionManager &session_manager,
                             WillPublisher &will_publisher,
                             EnhancedAuthRegistry &enhanced_auth_registry,
                             StructuredTracer &structured_tracer)
    : config_(config), active_auth_(active_auth), session_manager_(session_manager),
      will_publisher_(will_publisher),
      enhanced_auth_registry_(enhanced_auth_registry),
      structured_tracer_(structured_tracer) {}

ConnectResult ConnectFacade::handle_connect(const ConnectPacket &connect_packet,
                                            std::function<void()> close_callback) {
  std::lock_guard<std::mutex> lock_guard(mutex_);

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

  enhanced_auth_registry_.erase_client(result.client_id);

  if (EnhancedAuthHandler::is_enhanced(effective_connect)) {
    EnhancedAuthHandler auth_handler(std::shared_ptr<IAuthenticator>(
        &active_auth_, [](IAuthenticator * /*unused*/) {}));

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
      enhanced_auth_registry_.upsert_pending(
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
    enhanced_auth_registry_.upsert_active(completed.client_id,
                                          std::move(auth_handler));
    return return_with_trace(std::move(completed));
  }

  AuthResult auth_result{};
  try {
    auth_result = active_auth_.authenticate(effective_connect);
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

ConnectResult ConnectFacade::handle_auth_packet(std::string_view client_id,
                                                const AuthPacket &auth_packet) {
  std::lock_guard<std::mutex> lock_guard(mutex_);

  ConnectResult result;
  result.client_id = std::string(client_id);
  result.connack_properties = build_static_connack_properties(config_);

  enhanced_auth_registry_.with_lock(
      [this, &result, &auth_packet](
          auto &pending_enhanced_auth,
          auto &active_enhanced_auth) -> void {
        auto pending_iterator = pending_enhanced_auth.find(result.client_id);
        if (pending_iterator == pending_enhanced_auth.end()) {
          const AuthResult protocol_error = protocol_error_result();
          result.auth_status = protocol_error.status;
          result.reason_code = protocol_error.reason_code;
          return;
        }

        AuthResult auth_result{};
        try {
          auth_result = pending_iterator->second.handler.on_auth(auth_packet);
        } catch (const AuthException &exception) {
          result.auth_status = AuthStatus::Failure;
          result.reason_code = map_auth_error_to_reason(exception.error());
          pending_enhanced_auth.erase(pending_iterator);
          return;
        }

        result.auth_status = auth_result.status;
        result.reason_code = auth_result.reason_code;
        result.auth_data = auth_result.auth_data;
        result.auth_method =
            std::string(pending_iterator->second.handler.auth_method());

        if (auth_result.status == AuthStatus::Continue) {
          return;
        }

        if (auth_result.status == AuthStatus::Failure) {
          pending_enhanced_auth.erase(pending_iterator);
          return;
        }

        ConnectResult completed =
            complete_connect_success(pending_iterator->second.connect_packet,
                                     std::move(pending_iterator->second.close_callback));
        completed.auth_status = AuthStatus::Success;
        completed.auth_method =
            std::string(pending_iterator->second.handler.auth_method());
        append_connect_driven_connack_properties(
            pending_iterator->second.connect_packet, completed.connack_properties);
        append_authentication_method_property(completed.connack_properties,
                                              completed.auth_method);
        if (pending_iterator->second.assigned_client_id.has_value()) {
          completed.connack_properties.push_back(
              Property{.id = PropertyId::AssignedClientIdentifier,
                       .value =
                           Utf8String{*pending_iterator->second.assigned_client_id}});
        }
        active_enhanced_auth.insert_or_assign(
            completed.client_id, std::move(pending_iterator->second.handler));
        pending_enhanced_auth.erase(pending_iterator);
        result = std::move(completed);
      });

  return result;
}

AuthResult ConnectFacade::handle_reauthenticate(std::string_view client_id,
                                                const AuthPacket &auth_packet) {
  std::lock_guard<std::mutex> lock_guard(mutex_);

  AuthResult result = protocol_error_result();
  enhanced_auth_registry_.with_lock(
      [&result, client_id,
       &auth_packet](auto & /*pending_enhanced_auth*/,
                     auto &active_enhanced_auth) -> void {
        auto active_iterator = active_enhanced_auth.find(std::string(client_id));
        if (active_iterator == active_enhanced_auth.end()) {
          result = protocol_error_result();
          return;
        }

        try {
          result = active_iterator->second.reauthenticate(auth_packet);
        } catch (const AuthException &exception) {
          result = {.status = AuthStatus::Failure,
                    .reason_code = map_auth_error_to_reason(exception.error()),
                    .auth_data = {}};
          return;
        }

        if (result.status == AuthStatus::Failure) {
          active_enhanced_auth.erase(active_iterator);
        }
      });

  return result;
}

ReasonCode ConnectFacade::map_auth_error_to_reason(AuthError error_code) {
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

AuthResult ConnectFacade::protocol_error_result() {
  return {.status = AuthStatus::Failure,
          .reason_code = ReasonCode::ProtocolError,
          .auth_data = {}};
}

ConnectResult ConnectFacade::complete_connect_success(
    const ConnectPacket &connect_packet, std::function<void()> close_callback) {
  ConnectResult result;
  result.client_id = connect_packet.client_id.value;
  result.connack_properties = build_static_connack_properties(config_);

  try {
    const SessionOpenResult session_open =
        session_manager_.handle_connect(connect_packet, std::move(close_callback));
    result.session_present = session_open.session_present;
  } catch (const SessionManagerException &exception) {
    result.reason_code = map_session_error_to_reason(exception.error());
    result.auth_status = AuthStatus::Failure;
    return result;
  }

  if (connect_packet.will.has_value()) {
    will_publisher_.on_connect(result.client_id,
                               will_data_to_will_message(*connect_packet.will));
  }

  result.auth_status = AuthStatus::Success;
  return result;
}

void ConnectFacade::emit_connect_trace(const ConnectPacket &connect_packet,
                                       const ConnectResult &connect_result) noexcept {
  if (!structured_tracer_.should_emit(TraceLevel::Info, "broker")) {
    return;
  }

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

  structured_tracer_.emit(event);
}

} // namespace mqtt
