#include "message_router/subscriber_fanout.h"

#include <algorithm>

#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/types/variable_byte_integer.h"

namespace mqtt {

std::vector<DeliveryItem>
SubscriberFanout::prepare(const Message &msg,
                          const std::vector<MatchResult> &subscribers,
                          std::string_view publisher_client_id) {
  std::vector<DeliveryItem> items;
  items.reserve(subscribers.size());

  for (const auto &match : subscribers) {
    // 12.2.2 — No Local filter: skip when publisher == subscriber.
    if (match.subscription.options.no_local &&
        match.client_id == publisher_client_id) {
      continue;
    }

    items.push_back(DeliveryItem{
        .client_id = match.client_id,
        .message = apply_subscription_rules(msg, match.subscription),
    });
  }

  return items;
}

Message SubscriberFanout::apply_subscription_rules(const Message &msg,
                                                   const Subscription &sub) {
  Message out = msg;

  // 12.2.1 — QoS downgrade: cap at the subscription's maximum QoS.
  out.qos = std::min(out.qos, sub.qos);

  // 12.2.3 — Retain As Published: clear retain unless option is set.
  if (!sub.options.retain_as_published) {
    out.retain = false;
  }

  // 12.2.4 — Subscription Identifier: attach when present on subscription.
  if (sub.identifier.has_value()) {
    // Guard against duplicate SubscriptionIdentifier (should not occur on
    // inbound messages, but be defensive).
    auto dup_it =
        std::ranges::find_if(out.properties, [](const Property &prop) {
          return prop.id == PropertyId::SubscriptionIdentifier;
        });
    if (dup_it != out.properties.end()) {
      out.properties.erase(dup_it);
    }

    Property subscription_identifier_property{
      .id = PropertyId::SubscriptionIdentifier,
      .value = PropertyValue{VariableByteInteger{sub.identifier.value()}},
    };
    out.properties.push_back(subscription_identifier_property);
  }

  return out;
}

} // namespace mqtt
