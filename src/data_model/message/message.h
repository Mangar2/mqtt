#pragma once

#include <cstdint>
#include <vector>
#include "data_model/property/property.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

// Protocol-agnostic message (MQTT 5.0 Module 1.5.1).
// Represents publishable content without wire-format artifacts (no dup, no packet_id).
// Used by the message router, retained message store, and offline queue.
struct Message {
    Utf8String            topic;
    BinaryData            payload;
    QoS                   qos{QoS::AtMostOnce};
    bool                  retain{false};
    std::vector<Property> properties;  // PUBLISH-level properties

    bool operator==(const Message&) const noexcept = default;
};

// Will message stored independently of the CONNECT packet (MQTT 5.0 Module 1.5.2).
// Used by the Will Manager and Session Store.
// delay_interval: Will Delay Interval in seconds (extracted from WillDelayInterval property 0x18).
// All other will properties are stored in message.properties and travel with the PUBLISH.
struct WillMessage {
    Message  message;
    uint32_t delay_interval{0};

    bool operator==(const WillMessage&) const noexcept = default;
};

} // namespace mqtt
