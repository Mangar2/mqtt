#pragma once

/**
 * @file message.h
 * @brief Protocol-agnostic message types for routing, storage, and will handling (Module 1.5).
 */

#include <cstdint>
#include <vector>
#include "data_model/property/property.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

/**
 * @brief Protocol-agnostic MQTT message (Module 1.5.1).
 *
 * Represents publishable content without wire-format artifacts (no dup, no packet_id).
 * Used by the message router, retained message store, and offline queue.
 */
struct Message {
    Utf8String            topic;               ///< Topic name.
    BinaryData            payload;             ///< Application message payload.
    QoS                   qos{QoS::AtMostOnce};///< Delivery quality of service.
    bool                  retain{false};       ///< Retain flag.
    std::vector<Property> properties;          ///< PUBLISH-level properties.

    bool operator==(const Message&) const noexcept = default;
};

/**
 * @brief Will message stored independently of the CONNECT packet (Module 1.5.2).
 *
 * Used by the Will Manager and Session Store.
 * The Will Delay Interval (property 0x18) is extracted from the will properties and
 * stored as @p delay_interval; all other will properties travel with @p message.
 */
struct WillMessage {
    Message  message;            ///< Underlying message content and PUBLISH-level properties.
    uint32_t delay_interval{0};  ///< Will Delay Interval in seconds (from property 0x18).

    bool operator==(const WillMessage&) const noexcept = default;
};

} // namespace mqtt
