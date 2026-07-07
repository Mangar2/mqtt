#pragma once

/**
 * @file subscription_options.h
 * @brief MQTT 5.0 Subscription Options structure (Module 1.6.2).
 */

#include "data_model/subscription/retain_handling.h"

namespace mqtt {

/**
 * @brief Subscription options controlling message delivery behaviour
 * (Module 1.6.2).
 *
 * Encoded in the Subscription Options byte of the SUBSCRIBE packet
 * (MQTT 5.0 Section 3.8.3.1).
 */
struct SubscriptionOptions {
  bool no_local{false}; ///< Do not forward messages published by this client's
                        ///< own connection.
  bool retain_as_published{
      false}; ///< Forward the RETAIN flag as received from the publisher.
  RetainHandling retain_handling{
      RetainHandling::SendAtSubscribe}; ///< Controls retained message delivery
                                        ///< on subscribe.

  bool operator==(const SubscriptionOptions &other) const noexcept {
    return no_local == other.no_local &&
           retain_as_published == other.retain_as_published &&
           retain_handling == other.retain_handling;
  }
};

} // namespace mqtt
