#pragma once

/**
 * @file shared_subscription.h
 * @brief MQTT 5.0 Shared Subscription structure (Module 1.6.3).
 */

#include "data_model/types/utf8_string.h"

namespace mqtt {

/**
 * @brief A shared subscription binding a group name to a topic filter (Module 1.6.3).
 *
 * Models the `$share/<group>/<topic_filter>` syntax from MQTT 5.0 Section 4.8.2.
 * Multiple clients in the same @p group share message delivery so that each
 * message is forwarded to exactly one member. Round-robin dispatch is performed
 * by the Shared Subscription Dispatcher (Module 12.5).
 */
struct SharedSubscription {
    Utf8String group;         ///< Share group name; must not be empty.
    Utf8String topic_filter;  ///< Topic filter for the group; may contain `+` or `#` wildcards.

    bool operator==(const SharedSubscription&) const noexcept = default;
};

} // namespace mqtt
