#pragma once

/**
 * @file subscriber_fanout.h
 * @brief SubscriberFanout — applies per-subscription delivery rules and
 *        prepares outbound messages (Module 12.2).
 */

#include <string>
#include <string_view>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/subscription/subscription.h"
#include "topic/topic_matcher.h"

namespace mqtt {

/**
 * @brief An outbound delivery item produced by SubscriberFanout::prepare.
 */
struct DeliveryItem {
  std::string client_id; ///< Target client identifier.
  Message message;       ///< Message adjusted for this subscriber.
};

/**
 * @brief Applies per-subscription delivery rules and produces outbound
 *        DeliveryItem values (Module 12.2).
 *
 * Rules applied for each subscriber in order:
 * 1. **No Local** (12.2.2): omit the subscriber when the publisher and
 *    subscriber are the same client and the `no_local` option is set.
 * 2. **QoS downgrade** (12.2.1): cap outbound QoS at the subscription's
 *    maximum QoS.
 * 3. **Retain As Published** (12.2.3): clear `msg.retain` unless the
 *    `retain_as_published` option is set, in which case the original flag
 *    is preserved.
 * 4. **Subscription Identifier** (12.2.4): when the subscription carries an
 *    identifier, a SubscriptionIdentifier property is appended to the
 *    outbound message.
 *
 * Thread safety: none required — stateless; all methods are static.
 */
class SubscriberFanout {
public:
  /**
   * @brief Prepare outbound delivery items for all matching subscribers.
   *
   * @param msg                 Published message.
   * @param subscribers         Matching (client_id, Subscription) pairs from
   *                            SubscriptionStore.
   * @param publisher_client_id Client ID of the publisher (used for No Local
   *                            filter).
   * @return One DeliveryItem per subscriber that passes all filters.
   *         Subscribers blocked by No Local are omitted.
   */
  [[nodiscard]] static std::vector<DeliveryItem>
  prepare(const Message &msg, const std::vector<MatchResult> &subscribers,
          std::string_view publisher_client_id);

  /**
   * @brief Produce one outbound Message from a published message and a
   *        matching Subscription.
   *
   * @param msg Original published message.
   * @param sub The matching subscription.
   * @return Outbound message with QoS, retain, and identifier adjusted.
   */
  [[nodiscard]] static Message apply_subscription_rules(const Message &msg, const Subscription &sub);

private:
};

} // namespace mqtt
