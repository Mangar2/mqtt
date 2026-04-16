#pragma once

/**
 * @file subscription.h
 * @brief MQTT 5.0 Subscription structure (Module 1.6.1).
 */

#include <cstdint>
#include <optional>

#include "data_model/subscription/subscription_options.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

/**
 * @brief A single MQTT 5.0 subscription held by a session (Module 1.6.1).
 *
 * Associates a topic filter with a maximum delivery QoS and subscription options.
 * An optional Subscription Identifier links delivered messages back to this
 * subscription; the valid range is [1, 268 435 455] (MQTT 5.0 Section 3.8.4).
 */
struct Subscription {
    Utf8String          topic_filter;          ///< Topic filter; may contain `+` or `#` wildcards.
    QoS                 qos{QoS::AtMostOnce};  ///< Maximum delivery QoS for messages on this subscription.
    SubscriptionOptions options;               ///< Delivery options (No Local, Retain As Published, Retain Handling).
    std::optional<uint32_t> identifier;        ///< Subscription Identifier [1, 268 435 455]; absent if not provided.

    bool operator==(const Subscription&) const noexcept = default;
};

} // namespace mqtt
