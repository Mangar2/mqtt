#include "client/subscription_manager.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "data_model/types/utf8_string.h"
#include "topic/topic_matcher.h"
#include "topic/topic_error.h"
#include "topic/topic_validator.h"

namespace mqtt {

ClientSubscriptionManager::ClientSubscriptionManager(std::string client_id)
    : client_id_(std::move(client_id)) {
  if (client_id_.empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          "subscription manager requires non-empty client id");
  }
}

ClientSubscriptionManager::SubscribeOperation
ClientSubscriptionManager::begin_subscribe(
    const std::vector<SubscribeRequest> &requests) {
  if (requests.empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          "subscribe request list must not be empty");
  }

  SubscribeOperation subscribe_operation;
  subscribe_operation.packet_id = allocate_packet_id();
  subscribe_operation.packet.packet_id = subscribe_operation.packet_id;

  PendingSubscribe pending_subscribe;
  pending_subscribe.requests.reserve(requests.size());
  subscribe_operation.packet.filters.reserve(requests.size());

  for (const SubscribeRequest &request : requests) {
    validate_filter_or_throw(request.topic_filter);
    if (!request.callback) {
      release_packet_id(subscribe_operation.packet_id);
      throw ClientException(ClientError::InvalidPacket,
                            "subscribe request callback must be set");
    }

    SubscribeFilter subscribe_filter;
    subscribe_filter.topic_filter = Utf8String{request.topic_filter};
    subscribe_filter.options = to_wire_subscribe_options(request.options);
    subscribe_filter.options.max_qos = request.requested_qos;
    subscribe_operation.packet.filters.push_back(std::move(subscribe_filter));
    pending_subscribe.requests.push_back(request);
  }

  pending_subscribes_.insert_or_assign(subscribe_operation.packet_id,
                                       std::move(pending_subscribe));
  return subscribe_operation;
}

ClientSubscriptionManager::AckResult
ClientSubscriptionManager::on_suback(const SubackPacket &suback_packet) {
  const auto pending_iter = pending_subscribes_.find(suback_packet.packet_id);
  if (pending_iter == pending_subscribes_.end()) {
    throw ClientException(ClientError::ProtocolError,
                          "received SUBACK for unknown packet id");
  }

  const PendingSubscribe pending_subscribe = pending_iter->second;
  pending_subscribes_.erase(pending_iter);
  release_packet_id(suback_packet.packet_id);

  if (suback_packet.reason_codes.size() != pending_subscribe.requests.size()) {
    throw ClientException(ClientError::ProtocolError,
                          "SUBACK reason code count mismatch");
  }

  for (std::size_t request_index = 0U;
       request_index < pending_subscribe.requests.size(); ++request_index) {
    const ReasonCode reason_code = suback_packet.reason_codes[request_index];
    const std::optional<QoS> granted_qos = decode_granted_qos(reason_code);
    if (!granted_qos.has_value()) {
      continue;
    }
    activate_subscription(pending_subscribe.requests[request_index],
                          *granted_qos);
  }

  return AckResult{.packet_id = suback_packet.packet_id,
                   .reason_codes = suback_packet.reason_codes};
}

ClientSubscriptionManager::UnsubscribeOperation
ClientSubscriptionManager::begin_unsubscribe(
    const std::vector<std::string> &topic_filters) {
  if (topic_filters.empty()) {
    throw ClientException(ClientError::InvalidPacket,
                          "unsubscribe request list must not be empty");
  }

  UnsubscribeOperation unsubscribe_operation;
  unsubscribe_operation.packet_id = allocate_packet_id();
  unsubscribe_operation.packet.packet_id = unsubscribe_operation.packet_id;
  unsubscribe_operation.packet.topic_filters.reserve(topic_filters.size());

  PendingUnsubscribe pending_unsubscribe;
  pending_unsubscribe.topic_filters.reserve(topic_filters.size());

  for (const std::string &topic_filter : topic_filters) {
    validate_filter_or_throw(topic_filter);
    unsubscribe_operation.packet.topic_filters.push_back(Utf8String{topic_filter});
    pending_unsubscribe.topic_filters.push_back(topic_filter);
  }

  pending_unsubscribes_.insert_or_assign(unsubscribe_operation.packet_id,
                                         std::move(pending_unsubscribe));
  return unsubscribe_operation;
}

ClientSubscriptionManager::AckResult
ClientSubscriptionManager::on_unsuback(const UnsubackPacket &unsuback_packet) {
  const auto pending_iter = pending_unsubscribes_.find(unsuback_packet.packet_id);
  if (pending_iter == pending_unsubscribes_.end()) {
    throw ClientException(ClientError::ProtocolError,
                          "received UNSUBACK for unknown packet id");
  }

  const PendingUnsubscribe pending_unsubscribe = pending_iter->second;
  pending_unsubscribes_.erase(pending_iter);
  release_packet_id(unsuback_packet.packet_id);

  if (unsuback_packet.reason_codes.size() !=
      pending_unsubscribe.topic_filters.size()) {
    throw ClientException(ClientError::ProtocolError,
                          "UNSUBACK reason code count mismatch");
  }

  for (std::size_t filter_index = 0U;
       filter_index < pending_unsubscribe.topic_filters.size(); ++filter_index) {
    if (is_error(unsuback_packet.reason_codes[filter_index])) {
      continue;
    }
    remove_subscription(pending_unsubscribe.topic_filters[filter_index]);
  }

  return AckResult{.packet_id = unsuback_packet.packet_id,
                   .reason_codes = unsuback_packet.reason_codes};
}

std::size_t ClientSubscriptionManager::dispatch_inbound_publish(
    const PublishPacket &publish_packet) const {
  validate_topic_name_or_throw(publish_packet.topic.value);

  const std::vector<MatchResult> matches =
      TopicMatcher::match(subscription_trie_, publish_packet.topic.value);
  std::size_t callback_count = 0U;

  for (const MatchResult &match_result : matches) {
    const auto subscription_iter =
        active_subscriptions_.find(match_result.subscription.topic_filter.value);
    if (subscription_iter == active_subscriptions_.end()) {
      continue;
    }

    subscription_iter->second.callback(publish_packet);
    ++callback_count;
  }

  return callback_count;
}

bool ClientSubscriptionManager::has_subscription(
    std::string_view topic_filter) const noexcept {
  return active_subscriptions_.contains(std::string(topic_filter));
}

std::size_t ClientSubscriptionManager::subscription_count() const noexcept {
  return active_subscriptions_.size();
}

void ClientSubscriptionManager::clear() {
  packet_ids_in_use_.clear();
  pending_subscribes_.clear();
  pending_unsubscribes_.clear();
  active_subscriptions_.clear();
  subscription_trie_.remove_all(client_id_);
}

uint16_t ClientSubscriptionManager::allocate_packet_id() {
  constexpr uint32_t max_attempts =
      static_cast<uint32_t>(std::numeric_limits<uint16_t>::max());

  for (uint32_t attempt = 0U; attempt < max_attempts; ++attempt) {
    ++last_packet_id_;
    if (last_packet_id_ == 0U) {
      ++last_packet_id_;
    }
    if (!packet_ids_in_use_.contains(last_packet_id_)) {
      packet_ids_in_use_.insert(last_packet_id_);
      return last_packet_id_;
    }
  }

  throw ClientException(ClientError::ProtocolError,
                        "packet id space exhausted for subscription operations");
}

void ClientSubscriptionManager::release_packet_id(uint16_t packet_id) noexcept {
  packet_ids_in_use_.erase(packet_id);
}

SubscribeOptions ClientSubscriptionManager::to_wire_subscribe_options(
    const SubscriptionOptions &options) noexcept {
  SubscribeOptions subscribe_options;
  subscribe_options.no_local = options.no_local;
  subscribe_options.retain_as_published = options.retain_as_published;
  subscribe_options.retain_handling =
      static_cast<uint8_t>(options.retain_handling);
  return subscribe_options;
}

std::optional<QoS>
ClientSubscriptionManager::decode_granted_qos(ReasonCode reason_code) noexcept {
  switch (reason_code) {
  case ReasonCode::Success:
    return QoS::AtMostOnce;
  case ReasonCode::GrantedQoS1:
    return QoS::AtLeastOnce;
  case ReasonCode::GrantedQoS2:
    return QoS::ExactlyOnce;
  default:
    return std::nullopt;
  }
}

void ClientSubscriptionManager::validate_filter_or_throw(
    std::string_view topic_filter) {
  try {
    validate_topic_filter(topic_filter);
  } catch (const TopicException &) {
    throw ClientException(ClientError::InvalidPacket,
                          "invalid topic filter for subscription operation");
  }
}

void ClientSubscriptionManager::validate_topic_name_or_throw(
    std::string_view topic_name) {
  try {
    validate_topic_name(topic_name);
  } catch (const TopicException &) {
    throw ClientException(ClientError::InvalidPacket,
                          "invalid inbound publish topic name");
  }
}

void ClientSubscriptionManager::activate_subscription(
    const SubscribeRequest &request, QoS granted_qos) {
  Subscription subscription;
  subscription.topic_filter = Utf8String{request.topic_filter};
  subscription.qos = granted_qos;
  subscription.options = request.options;

  remove_subscription(request.topic_filter);
  subscription_trie_.insert(client_id_, subscription);
  active_subscriptions_.insert_or_assign(
      request.topic_filter,
      ActiveSubscription{.subscription = std::move(subscription),
                         .callback = request.callback});
}

void ClientSubscriptionManager::remove_subscription(std::string_view topic_filter) {
  active_subscriptions_.erase(std::string(topic_filter));
  subscription_trie_.remove(client_id_, topic_filter);
}

} // namespace mqtt
