#include "broker/publish_facade.h"

#include <string>

#include "data_model/property/property_id.h"
#include "message_router/message_router_error.h"

namespace mqtt {

namespace {

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

} // namespace

PublishFacade::PublishFacade(MessageRouter &message_router,
                             StatisticsCollector &statistics_collector,
                             StructuredTracer &structured_tracer)
    : message_router_(message_router), statistics_collector_(statistics_collector),
      structured_tracer_(structured_tracer) {}

ReasonCode PublishFacade::handle_publish(Message &message,
                                         std::string_view client_id,
                                         std::string_view username,
                                         TopicAliasTable &alias_table) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  statistics_collector_.on_message_inbound();

  TRACE_GUARD(&structured_tracer_, TraceLevel::Trace, "broker") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "broker";
    event.info = "publish_received";
    event.data.emplace_back("client_id", std::string(client_id));
    event.data.emplace_back("topic", message.topic.value);
    event.data.emplace_back("qos", std::to_string(static_cast<int>(message.qos)));
    event.data.emplace_back("payload_bytes",
                            std::to_string(message.payload.data.size()));
    structured_tracer_.emit(event);
  }

  if (has_zero_topic_alias_property(message)) {
    TRACE_GUARD(&structured_tracer_, TraceLevel::Trace, "broker") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "broker";
      event.info = "publish_rejected_zero_topic_alias";
      event.data.emplace_back("client_id", std::string(client_id));
      event.data.emplace_back("topic", message.topic.value);
      event.data.emplace_back("payload_bytes",
                              std::to_string(message.payload.data.size()));
      structured_tracer_.emit(event);
    }
    return ReasonCode::ImplementationSpecificError;
  }

  try {
    const bool has_matching_subscribers =
        message_router_.route(message, client_id, username, alias_table);
    TRACE_GUARD(&structured_tracer_, TraceLevel::Trace, "broker") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "broker";
      event.info = "publish_routed";
      event.data.emplace_back("client_id", std::string(client_id));
      event.data.emplace_back("topic", message.topic.value);
      event.data.emplace_back("payload_bytes",
                              std::to_string(message.payload.data.size()));
      event.data.emplace_back("has_matching_subscribers",
                              has_matching_subscribers ? "true" : "false");
      structured_tracer_.emit(event);
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
    return ReasonCode::ProtocolError;
  }
}

} // namespace mqtt
