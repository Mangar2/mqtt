#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

// PUBLISH packet (MQTT 5.0 Section 3.3).
struct PublishPacket {
    bool                       dup{false};
    QoS                        qos{QoS::AtMostOnce};
    bool                       retain{false};
    Utf8String                 topic;
    std::optional<uint16_t>    packet_id;  // present iff qos > AtMostOnce
    BinaryData                 payload;
    std::vector<Property>      properties;

    bool operator==(const PublishPacket&) const noexcept = default;
};

// PUBACK packet (MQTT 5.0 Section 3.4) — QoS 1 acknowledgement.
struct PubackPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubackPacket&) const noexcept = default;
};

// PUBREC packet (MQTT 5.0 Section 3.5) — QoS 2 step 1 response.
struct PubrecPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubrecPacket&) const noexcept = default;
};

// PUBREL packet (MQTT 5.0 Section 3.6) — QoS 2 step 2 release.
struct PubrelPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubrelPacket&) const noexcept = default;
};

// PUBCOMP packet (MQTT 5.0 Section 3.7) — QoS 2 step 3 complete.
struct PubcompPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubcompPacket&) const noexcept = default;
};

} // namespace mqtt
