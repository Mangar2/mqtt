#pragma once

#include <cstdint>
#include <vector>
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

// Per-filter subscription options (MQTT 5.0 Section 3.8.3.1).
struct SubscriptionOptions {
    QoS     max_qos{QoS::AtMostOnce};
    bool    no_local{false};           // do not deliver publisher's own messages
    bool    retain_as_published{false};// forward the RETAIN flag as received
    uint8_t retain_handling{0};        // 0 = send on subscribe, 1 = only if new, 2 = never

    bool operator==(const SubscriptionOptions&) const noexcept = default;
};

// One topic filter + options entry in a SUBSCRIBE packet.
struct SubscribeFilter {
    Utf8String          topic_filter;
    SubscriptionOptions options;

    bool operator==(const SubscribeFilter&) const noexcept = default;
};

// SUBSCRIBE packet (MQTT 5.0 Section 3.8).
struct SubscribePacket {
    uint16_t                    packet_id{0};
    std::vector<Property>       properties;
    std::vector<SubscribeFilter> filters;   // at least one required by the spec

    bool operator==(const SubscribePacket&) const noexcept = default;
};

// SUBACK packet (MQTT 5.0 Section 3.9).
// reason_codes has one entry per filter in the corresponding SUBSCRIBE.
struct SubackPacket {
    uint16_t                 packet_id{0};
    std::vector<Property>    properties;
    std::vector<ReasonCode>  reason_codes;

    bool operator==(const SubackPacket&) const noexcept = default;
};

// UNSUBSCRIBE packet (MQTT 5.0 Section 3.10).
struct UnsubscribePacket {
    uint16_t                 packet_id{0};
    std::vector<Property>    properties;
    std::vector<Utf8String>  topic_filters;  // at least one required by the spec

    bool operator==(const UnsubscribePacket&) const noexcept = default;
};

// UNSUBACK packet (MQTT 5.0 Section 3.11).
// reason_codes has one entry per filter in the corresponding UNSUBSCRIBE.
struct UnsubackPacket {
    uint16_t                 packet_id{0};
    std::vector<Property>    properties;
    std::vector<ReasonCode>  reason_codes;

    bool operator==(const UnsubackPacket&) const noexcept = default;
};

} // namespace mqtt
