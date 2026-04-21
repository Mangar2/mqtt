#include "broker/disconnect_facade.h"

#include <string>
#include <vector>

#include "connection/outbound_queue_bridge.h"

namespace mqtt {

DisconnectFacade::DisconnectFacade(
    WillPublisher &will_publisher, SessionManager &session_manager,
    EnhancedAuthRegistry &enhanced_auth_registry,
    ActiveConnectionRegistry &connection_registry, MessageRouter &message_router,
    SharedSubscriptionDispatcher &shared_dispatcher,
    StatisticsCollector &statistics_collector, StructuredTracer &structured_tracer)
    : will_publisher_(will_publisher), session_manager_(session_manager),
      enhanced_auth_registry_(enhanced_auth_registry),
      connection_registry_(connection_registry), message_router_(message_router),
      shared_dispatcher_(shared_dispatcher),
      statistics_collector_(statistics_collector),
      structured_tracer_(structured_tracer) {}

void DisconnectFacade::handle_disconnect(
    std::string_view client_id, ReasonCode reason_code,
    std::optional<uint32_t> expiry_override,
    std::chrono::steady_clock::time_point now,
    const std::shared_ptr<OutboundQueue> &expected_queue) {
  TRACE_GUARD(&structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "disconnect_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("reason_code",
                            std::to_string(static_cast<int>(reason_code)));
    event.data.emplace_back("expiry_override",
                            expiry_override.has_value()
                                ? std::to_string(*expiry_override)
                                : "<unset>");
    structured_tracer_.emit(event);
  }
  enhanced_auth_registry_.erase_client(client_id);
  will_publisher_.on_disconnect(client_id, reason_code, now);
  unregister_connection_impl(client_id, expected_queue);
  session_manager_.handle_disconnect(client_id, expiry_override, now);
}

bool DisconnectFacade::is_disconnect_expiry_override_valid(
    std::string_view client_id, std::optional<uint32_t> expiry_override) {
  return session_manager_.is_disconnect_expiry_override_valid(client_id,
                                                              expiry_override);
}

void DisconnectFacade::handle_connection_lost(
    std::string_view client_id, std::chrono::steady_clock::time_point now,
    const std::shared_ptr<OutboundQueue> &expected_queue) {
  TRACE_GUARD(&structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connection_lost_handled";
    event.data.push_back({"client_id", std::string(client_id)});
    structured_tracer_.emit(event);
  }
  enhanced_auth_registry_.erase_client(client_id);
  will_publisher_.on_connection_lost(client_id, now);
  unregister_connection_impl(client_id, expected_queue);
  session_manager_.handle_disconnect(client_id, std::nullopt, now);
}

void DisconnectFacade::unregister_connection(
    std::string_view client_id,
    const std::shared_ptr<OutboundQueue> &expected_queue) {
  unregister_connection_impl(client_id, expected_queue);
}

void DisconnectFacade::unregister_connection_impl(
    std::string_view client_id,
    const std::shared_ptr<OutboundQueue> &expected_queue) {
  const ConnectionRemoveResult remove_result =
      connection_registry_.remove_if_matches(client_id, expected_queue);
  if (!remove_result.removed) {
    return;
  }

  std::size_t moved_to_offline = 0U;
  if (remove_result.removed_queue) {
    std::vector<Message> pending_messages =
        drain_pending_outbound_messages(*remove_result.removed_queue);
    moved_to_offline = message_router_.buffer_offline_messages(
        client_id, std::move(pending_messages));
  }

  TRACE_GUARD(&structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "connection_unregistered";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("moved_to_offline", std::to_string(moved_to_offline));
    event.data.emplace_back(
        "active_connections_before",
        std::to_string(remove_result.active_connections_before));
    structured_tracer_.emit(event);
  }

  shared_dispatcher_.remove_client(client_id);
  const std::size_t erase_count = 1U;
  if (erase_count > 0U) {
    statistics_collector_.on_client_disconnected();
  }
}

} // namespace mqtt
