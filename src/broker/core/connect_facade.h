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
    * @param connect_packet CONNECT packet from client.
    * @param close_callback Callback used for takeover close operations.
    * @return CONNECT handling result.
   */
  [[nodiscard]] ConnectResult handle_connect(const ConnectPacket &connect_packet, std::function<void()> close_callback);

  /**
   * @brief Handle AUTH continuation for a pending enhanced-auth exchange.
    * @param client_id Client identifier.
    * @param auth_packet Incoming AUTH packet.
    * @return CONNECT handling continuation result.
   */
  [[nodiscard]] ConnectResult handle_auth_packet(std::string_view client_id,
                                                 const AuthPacket &auth_packet);

  /**
   * @brief Handle AUTH re-authentication for an active enhanced-auth session.
   * @param client_id Client identifier.
   * @param auth_packet Incoming AUTH packet.
   * @return Re-authentication result.
   */
  [[nodiscard]] AuthResult handle_reauthenticate(std::string_view client_id,
                                                 const AuthPacket &auth_packet);

private:
  /**
   * @brief Map auth-layer error to MQTT reason code.
   * @param error_code Auth-layer error code.
   * @return Mapped MQTT reason code.
   */
  [[nodiscard]] static ReasonCode map_auth_error_to_reason(AuthError error_code);

  /**
   * @brief Build protocol-error auth result.
   * @return Auth result for protocol error path.
   */
  [[nodiscard]] static AuthResult protocol_error_result();

  /**
   * @brief Finalize successful connect path after auth checks.
   * @param connect_packet CONNECT packet from client.
   * @param close_callback Callback used for takeover close operations.
   * @return Successful connect result.
   */
  [[nodiscard]] ConnectResult complete_connect_success(const ConnectPacket &connect_packet, std::function<void()> close_callback);

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
