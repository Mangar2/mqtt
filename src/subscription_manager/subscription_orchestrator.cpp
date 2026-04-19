#include "subscription_manager/subscription_orchestrator.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "data_model/subscription/retain_handling.h"
#include "topic/topic_error.h"
#include "topic/topic_validator.h"

namespace mqtt {

namespace {

struct SharedSubscriptionFilter {
  std::string group;
  std::string topic_filter;
};

[[nodiscard]] std::optional<SharedSubscriptionFilter>
parse_shared_subscription_filter(std::string_view topic_filter) {
  constexpr std::string_view k_shared_prefix = "$share/";
  if (!topic_filter.starts_with(k_shared_prefix)) {
    return std::nullopt;
  }

  const std::string_view remainder = topic_filter.substr(k_shared_prefix.size());
  const std::size_t group_separator = remainder.find('/');
  if (group_separator == std::string_view::npos) {
    return std::nullopt;
  }

  if (group_separator == 0U || group_separator + 1U >= remainder.size()) {
    return std::nullopt;
  }

  SharedSubscriptionFilter shared_filter;
  shared_filter.group = std::string(remainder.substr(0U, group_separator));
  shared_filter.topic_filter =
      std::string(remainder.substr(group_separator + 1U));
  return shared_filter;
}

} // namespace

SubscriptionOrchestrator::SubscriptionOrchestrator(
    AclEngine &acl_engine, SubscriptionStore &subscription_store,
    SharedSubscriptionDispatcher &shared_dispatcher,
    MessageRouter &message_router) noexcept
    : acl_engine_(acl_engine), subscription_store_(subscription_store),
      shared_dispatcher_(shared_dispatcher), message_router_(message_router) {}

SubackPacket SubscriptionOrchestrator::handle_subscribe(
    std::string_view client_id, const SubscribePacket &packet) {
  SubackPacket suback;
  suback.packet_id = packet.packet_id;

  const auto subscription_identifier = subscription_identifier_from(packet);
  if (subscription_identifier.has_value() && *subscription_identifier == 0U) {
    throw std::runtime_error(
        "SUBSCRIBE protocol error: Subscription Identifier must be non-zero");
  }

  for (const SubscribeFilter &filter : packet.filters) {
    const auto shared_filter =
        parse_shared_subscription_filter(filter.topic_filter.value);
    if (filter.topic_filter.value.starts_with("$share/") &&
        !shared_filter.has_value()) {
      suback.reason_codes.push_back(ReasonCode::TopicFilterInvalid);
      continue;
    }

    if (shared_filter.has_value() && filter.options.no_local) {
      throw std::runtime_error(
          "SUBSCRIBE protocol error: No Local is not valid on shared subscriptions");
    }

    const std::string_view effective_filter =
        shared_filter.has_value()
            ? std::string_view(shared_filter->topic_filter)
            : std::string_view(filter.topic_filter.value);

    try {
      validate_topic_filter(effective_filter);
    } catch (const TopicException & /*unused*/) {
      suback.reason_codes.push_back(ReasonCode::TopicFilterInvalid);
      continue;
    }

    const bool allowed = acl_engine_.check_subscribe(client_id, "", effective_filter);
    if (!allowed) {
      suback.reason_codes.push_back(ReasonCode::NotAuthorized);
      continue;
    }

    Subscription subscription;
    subscription.topic_filter = Utf8String{std::string(effective_filter)};
    subscription.qos = filter.options.max_qos;
    subscription.options.no_local = filter.options.no_local;
    subscription.options.retain_as_published =
        filter.options.retain_as_published;
    subscription.options.retain_handling =
        static_cast<RetainHandling>(filter.options.retain_handling);
    subscription.identifier = subscription_identifier;

    if (shared_filter.has_value()) {
      shared_dispatcher_.add_member(shared_filter->group,
                                    shared_filter->topic_filter,
                                    client_id,
                                    subscription);
    } else {
      const bool is_new_subscription =
          subscription_store_.store(client_id, subscription);
      message_router_.deliver_retained(client_id,
                                       filter.topic_filter.value,
                                       subscription,
                                       is_new_subscription);
    }

    suback.reason_codes.push_back(qos_to_granted_reason(filter.options.max_qos));
  }

  return suback;
}

UnsubackPacket SubscriptionOrchestrator::handle_unsubscribe(
    std::string_view client_id, const UnsubscribePacket &packet) {
  UnsubackPacket unsuback;
  unsuback.packet_id = packet.packet_id;

  for (const Utf8String &topic_filter : packet.topic_filters) {
    const auto shared_filter =
        parse_shared_subscription_filter(topic_filter.value);
    if (topic_filter.value.starts_with("$share/") &&
        !shared_filter.has_value()) {
      unsuback.reason_codes.push_back(ReasonCode::TopicFilterInvalid);
      continue;
    }

    const std::string_view effective_filter =
        shared_filter.has_value()
            ? std::string_view(shared_filter->topic_filter)
            : std::string_view(topic_filter.value);

    try {
      validate_topic_filter(effective_filter);
    } catch (const TopicException & /*unused*/) {
      unsuback.reason_codes.push_back(ReasonCode::TopicFilterInvalid);
      continue;
    }

    bool found = false;
    if (shared_filter.has_value()) {
      const std::size_t members_before = shared_dispatcher_.member_count(
          shared_filter->group, shared_filter->topic_filter);
      shared_dispatcher_.remove_member(shared_filter->group,
                                       shared_filter->topic_filter,
                                       client_id);
      const std::size_t members_after = shared_dispatcher_.member_count(
          shared_filter->group, shared_filter->topic_filter);
      found = members_after < members_before;
    } else {
      found = subscription_store_.remove(client_id, topic_filter.value);
    }

    unsuback.reason_codes.push_back(
        found ? ReasonCode::Success : ReasonCode::NoSubscriptionFound);
  }

  return unsuback;
}

} // namespace mqtt
