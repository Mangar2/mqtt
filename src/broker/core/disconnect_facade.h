#pragma once

/**
 * @file disconnect_facade.h
 * @brief Disconnect/connection-loss facade extracted from Broker.
 */

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include "broker/active_connection_registry.h"
#include "broker/enhanced_auth_registry.h"
#include "data_model/reason_code/reason_code.h"
#include "message_router/message_router.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/structured_tracer.h"
#include "outbound_queue/outbound_queue.h"
#include "session_manager/session_manager.h"
#include "will_manager/will_publisher.h"

namespace mqtt {

/**
 * @brief Thread-safe disconnect facade.
 */
class DisconnectFacade {
public:
  /**
   * @brief Construct a disconnect facade over broker dependencies.
   */
  DisconnectFacade(WillPublisher &will_publisher, SessionManager &session_manager,
                   EnhancedAuthRegistry &enhanced_auth_registry,
                   ActiveConnectionRegistry &connection_registry,
                   MessageRouter &message_router,
                   SharedSubscriptionDispatcher &shared_dispatcher,
                   StatisticsCollector &statistics_collector,
                   StructuredTracer &structured_tracer);

  /**
   * @brief Handle DISCONNECT workflow.
   */
  void handle_disconnect(std::string_view client_id, ReasonCode reason_code,
                         std::optional<uint32_t> expiry_override,
                         std::chrono::steady_clock::time_point now,
                         const std::shared_ptr<OutboundQueue> &expected_queue);

  /**
   * @brief Validate DISCONNECT session-expiry override.
   */
  [[nodiscard]] bool
  is_disconnect_expiry_override_valid(std::string_view client_id,
                                      std::optional<uint32_t> expiry_override);

  /**
   * @brief Handle abrupt connection loss workflow.
   */
  void handle_connection_lost(
      std::string_view client_id, std::chrono::steady_clock::time_point now,
      const std::shared_ptr<OutboundQueue> &expected_queue);

  /**
   * @brief Unregister connection and move pending messages to offline buffer.
   */
  void unregister_connection(std::string_view client_id,
                             const std::shared_ptr<OutboundQueue> &expected_queue);

private:
  void unregister_connection_impl(
      std::string_view client_id,
      const std::shared_ptr<OutboundQueue> &expected_queue);

  WillPublisher &will_publisher_;
  SessionManager &session_manager_;
  EnhancedAuthRegistry &enhanced_auth_registry_;
  ActiveConnectionRegistry &connection_registry_;
  MessageRouter &message_router_;
  SharedSubscriptionDispatcher &shared_dispatcher_;
  StatisticsCollector &statistics_collector_;
  StructuredTracer &structured_tracer_;
};

} // namespace mqtt
