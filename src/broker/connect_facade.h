#pragma once

/**
 * @file connect_facade.h
 * @brief CONNECT/AUTH/re-authentication facade extracted from Broker.
 */

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "auth/auth_error.h"
#include "auth/authenticator.h"
#include "broker/broker_config.h"
#include "broker/connect_result.h"
#include "broker/enhanced_auth_registry.h"
#include "data_model/packet/connect_packet.h"
#include "monitoring/structured_tracer.h"
#include "session_manager/session_manager.h"
#include "will_manager/will_publisher.h"

namespace mqtt {

/**
 * @brief Thread-safe facade for CONNECT and AUTH packet workflows.
 */
class ConnectFacade {
public:
  /**
   * @brief Construct a connect facade over broker dependencies.
   */
  ConnectFacade(const BrokerConfig &config, IAuthenticator &active_auth,
                SessionManager &session_manager, WillPublisher &will_publisher,
                EnhancedAuthRegistry &enhanced_auth_registry,
                StructuredTracer &structured_tracer);

  /**
   * @brief Handle CONNECT (including enhanced-auth first step).
   */
  [[nodiscard]] ConnectResult
  handle_connect(const ConnectPacket &connect_packet,
                 std::function<void()> close_callback);

  /**
   * @brief Handle AUTH continuation for a pending enhanced-auth exchange.
   */
  [[nodiscard]] ConnectResult handle_auth_packet(std::string_view client_id,
                                                 const AuthPacket &auth_packet);

  /**
   * @brief Handle AUTH re-authentication for an active enhanced-auth session.
   */
  [[nodiscard]] AuthResult
  handle_reauthenticate(std::string_view client_id, const AuthPacket &auth_packet);

private:
  [[nodiscard]] static ReasonCode map_auth_error_to_reason(AuthError error_code);

  [[nodiscard]] static AuthResult protocol_error_result();

  [[nodiscard]] ConnectResult
  complete_connect_success(const ConnectPacket &connect_packet,
                           std::function<void()> close_callback);

  void emit_connect_trace(const ConnectPacket &connect_packet,
                          const ConnectResult &connect_result) noexcept;

  const BrokerConfig &config_;
  IAuthenticator &active_auth_;
  SessionManager &session_manager_;
  WillPublisher &will_publisher_;
  EnhancedAuthRegistry &enhanced_auth_registry_;
  StructuredTracer &structured_tracer_;
};

} // namespace mqtt
