#include "message_router/inbound_publish_processor.h"

#include <algorithm>
#include <cstdint>

#include "authz/broker_acl_policy.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "message_router/message_router_error.h"
#include "topic/topic_validator.h"

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

  if (client_id != k_broker_internal_principal && is_system_topic(msg.topic.value)) {
    throw MessageRouterException(MessageRouterError::PublishNotAuthorized,
                                 "clients cannot publish to $SYS topics");
  }

  if (!acl_.check_publish(client_id, username, msg.topic.value)) {
    throw MessageRouterException(MessageRouterError::PublishNotAuthorized,
                                 "client not authorized to publish to topic");
  }

  if (msg.retain) {
    retained_.store(msg);
    emit_on_retained_changed();
  }

  return subscriptions_.subscribers_for(msg.topic.value);
}

std::vector<RetainedMessageRecord> InboundPublishProcessor::retained_for_filter(
    std::string_view topic_filter) const {
  return retained_.find_records(topic_filter);
}

void InboundPublishProcessor::set_on_retained_changed(
    std::function<void()> callback) noexcept {
  set_on_retained_changed_callback(std::move(callback));
}

std::function<void()>
InboundPublishProcessor::snapshot_on_retained_changed() const {
  std::lock_guard<std::mutex> lock(on_retained_change_callback_mutex_);
  return on_retained_changed_;
}

void InboundPublishProcessor::set_on_retained_changed_callback(
    std::function<void()> callback) noexcept {
  std::lock_guard<std::mutex> lock(on_retained_change_callback_mutex_);
  on_retained_changed_ = std::move(callback);
}

void InboundPublishProcessor::emit_on_retained_changed() const noexcept {
  std::function<void()> callback = snapshot_on_retained_changed();
  if (!callback) {
    return;
  }

  try {
    callback();
  } catch (...) {
  }
}

} // namespace mqtt
