#pragma once

/**
 * @file retain_handling.h
 * @brief MQTT 5.0 Retain Handling option for subscriptions (Module 1.6.2).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief Controls when retained messages are forwarded upon subscription (MQTT 5.0 Section 3.8.3.1).
 *
 * Encoded in bits [4:5] of the Subscription Options byte.
 */
enum class RetainHandling : uint8_t {
    SendAtSubscribe = 0,  ///< Send retained messages at the time of subscribe.
    SendIfNew       = 1,  ///< Send retained messages only if the subscription is new.
    Never           = 2,  ///< Never send retained messages on subscribe.
};

} // namespace mqtt
