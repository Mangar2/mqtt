#include "client/outbound_topic_alias_manager.h"

#include <vector>

#include "client/client_error.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"

namespace mqtt {

namespace {

constexpr uint16_t k_invalid_alias = 0U;

} // namespace

OutboundTopicAliasManager::OutboundTopicAliasManager(uint16_t max_aliases) noexcept
    : max_aliases_(max_aliases), next_alias_(1U) {}

bool OutboundTopicAliasManager::apply(PublishPacket &publish_packet) {
  if (max_aliases_ == 0U) {
    return false;
  }

  if (publish_packet.topic.value.empty()) {
    throw ClientException(
        ClientError::InvalidPacket,
        "outbound topic alias assignment requires non-empty topic name");
  }

  const std::string topic_name = publish_packet.topic.value;
  const auto topic_iter = topic_to_alias_.find(topic_name);

  if (topic_iter != topic_to_alias_.end()) {
    set_topic_alias_property(publish_packet, topic_iter->second);
    publish_packet.topic.value.clear();
    return true;
  }

  const uint16_t new_alias = allocate_alias();
  bind_alias(new_alias, topic_name);
  set_topic_alias_property(publish_packet, new_alias);
  return true;
}

void OutboundTopicAliasManager::reset() noexcept {
  topic_to_alias_.clear();
  alias_to_topic_.clear();
  next_alias_ = 1U;
}

uint16_t OutboundTopicAliasManager::max_aliases() const noexcept {
  return max_aliases_;
}

uint16_t OutboundTopicAliasManager::allocate_alias() {
  if (max_aliases_ == 0U) {
    throw ClientException(ClientError::AliasOutOfRange,
                          "topic alias maximum is zero");
  }

  if (topic_to_alias_.size() >= static_cast<std::size_t>(max_aliases_)) {
    const uint16_t reused_alias = next_alias_;
    advance_next_alias();
    return reused_alias;
  }

  for (uint16_t scan_count = 0U; scan_count < max_aliases_; ++scan_count) {
    const uint16_t candidate_alias = next_alias_;
    advance_next_alias();
    if (!alias_to_topic_.contains(candidate_alias)) {
      return candidate_alias;
    }
  }

  throw ClientException(ClientError::AliasOutOfRange,
                        "no free topic alias available");
}

void OutboundTopicAliasManager::bind_alias(uint16_t alias_value,
                                           const std::string &topic_name) {
  if (alias_value == k_invalid_alias || alias_value > max_aliases_) {
    throw ClientException(ClientError::AliasOutOfRange,
                          "topic alias out of negotiated range");
  }

  const auto alias_iter = alias_to_topic_.find(alias_value);
  if (alias_iter != alias_to_topic_.end()) {
    topic_to_alias_.erase(alias_iter->second);
  }

  alias_to_topic_.insert_or_assign(alias_value, topic_name);
  topic_to_alias_.insert_or_assign(topic_name, alias_value);
}

void OutboundTopicAliasManager::set_topic_alias_property(
    PublishPacket &publish_packet, uint16_t alias_value) const {
  for (Property &property : publish_packet.properties) {
    if (property.id == PropertyId::TopicAlias) {
      property.value = alias_value;
      return;
    }
  }

  publish_packet.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = alias_value});
}

void OutboundTopicAliasManager::advance_next_alias() noexcept {
  ++next_alias_;
  if (next_alias_ == 0U || next_alias_ > max_aliases_) {
    next_alias_ = 1U;
  }
}

} // namespace mqtt
