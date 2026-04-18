#include "message_router/inbound_publish_processor.h"

#include <algorithm>
#include <cstdint>

#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "message_router/message_router_error.h"

namespace mqtt {

InboundPublishProcessor::InboundPublishProcessor(
    AclEngine &acl, RetainedMessageStore &retained,
    SubscriptionStore &subscriptions)
    : acl_(acl), retained_(retained), subscriptions_(subscriptions) {}

void InboundPublishProcessor::resolve_topic_alias(
    Message &msg, TopicAliasTable &alias_table) {
  auto alias_it =
      std::ranges::find_if(msg.properties, [](const Property &prop) {
        return prop.id == PropertyId::TopicAlias;
      });

  if (alias_it == msg.properties.end()) {
    return; // No alias property — topic is already set.
  }

  const uint16_t alias_val = std::get<uint16_t>(alias_it->value);

  if (msg.topic.value.empty()) {
    // Alias-only PUBLISH — look up existing mapping.
    try {
      msg.topic.value = alias_table.get_inbound(alias_val);
    } catch (...) {
      throw MessageRouterException(MessageRouterError::TopicAliasInvalid,
                                   "unregistered or invalid Topic Alias");
    }
  } else {
    // New alias registration — topic string is also present.
    try {
      alias_table.set_inbound(alias_val, msg.topic.value);
    } catch (...) {
      throw MessageRouterException(MessageRouterError::TopicAliasInvalid,
                                   "Topic Alias value out of range");
    }
  }

  // Remove the alias property — outbound PUBLISH must not carry it.
  msg.properties.erase(alias_it);
}

std::vector<MatchResult>
InboundPublishProcessor::process(Message &msg, std::string_view client_id,
                                 std::string_view username,
                                 TopicAliasTable &alias_table) {
  resolve_topic_alias(msg, alias_table);

  if (!acl_.check_publish(client_id, username, msg.topic.value)) {
    throw MessageRouterException(MessageRouterError::PublishNotAuthorized,
                                 "client not authorized to publish to topic");
  }

  if (msg.retain) {
    retained_.store(msg);
  }

  return subscriptions_.subscribers_for(msg.topic.value);
}

std::vector<Message> InboundPublishProcessor::retained_for_filter(
    std::string_view topic_filter) const {
  return retained_.find(topic_filter);
}

} // namespace mqtt
