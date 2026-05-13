#include "broker/subscribe_facade.h"

#include <string>
#include <sstream>

namespace mqtt {

SubscribeFacade::SubscribeFacade(
    SubscriptionOrchestrator &subscription_orchestrator,
    StructuredTracer &structured_tracer)
    : subscription_orchestrator_(subscription_orchestrator),
      structured_tracer_(structured_tracer) {}

SubackPacket SubscribeFacade::handle_subscribe(std::string_view client_id,
                                               const SubscribePacket &packet) {
  SubackPacket suback =
      subscription_orchestrator_.handle_subscribe(client_id, packet);

  std::size_t failure_count = 0U;
  for (const ReasonCode reason_code : suback.reason_codes) {
    if (is_error(reason_code)) {
      ++failure_count;
    }
  }

  // Build topic_filters list (max 10 for performance)
  std::ostringstream topics_stream;
  const std::size_t max_filters_to_log = 10U;
  for (std::size_t idx = 0U; idx < packet.filters.size(); ++idx) {
    if (idx >= max_filters_to_log) {
      topics_stream << ",...";
      break;
    }
    if (idx > 0U) {
      topics_stream << ",";
    }
    topics_stream << packet.filters[idx].topic_filter.value;
  }

  TRACE_GUARD(&structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "subscribe_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("requested_filters",
                            std::to_string(packet.filters.size()));
    event.data.emplace_back(
        "granted_filters",
        std::to_string(suback.reason_codes.size() - failure_count));
    event.data.emplace_back("failed_filters", std::to_string(failure_count));
    event.data.emplace_back("topic_filters", topics_stream.str());
    structured_tracer_.emit(event);
  }

  return suback;
}

UnsubackPacket SubscribeFacade::handle_unsubscribe(
    std::string_view client_id, const UnsubscribePacket &packet) {
  UnsubackPacket unsuback =
      subscription_orchestrator_.handle_unsubscribe(client_id, packet);

  std::size_t failure_count = 0U;
  for (const ReasonCode reason_code : unsuback.reason_codes) {
    if (is_error(reason_code)) {
      ++failure_count;
    }
  }

  // Build topic_filters list (max 10 for performance)
  std::ostringstream topics_stream;
  const std::size_t max_filters_to_log = 10U;
  for (std::size_t idx = 0U; idx < packet.topic_filters.size(); ++idx) {
    if (idx >= max_filters_to_log) {
      topics_stream << ",...";
      break;
    }
    if (idx > 0U) {
      topics_stream << ",";
    }
    topics_stream << packet.topic_filters[idx].value;
  }

  TRACE_GUARD(&structured_tracer_, TraceLevel::Info, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "broker";
    event.info = "unsubscribe_handled";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("requested_filters",
                            std::to_string(packet.topic_filters.size()));
    event.data.emplace_back(
        "successful_filters",
        std::to_string(unsuback.reason_codes.size() - failure_count));
    event.data.emplace_back("failed_filters", std::to_string(failure_count));
    event.data.emplace_back("topic_filters", topics_stream.str());
    structured_tracer_.emit(event);
  }

  return unsuback;
}

} // namespace mqtt
