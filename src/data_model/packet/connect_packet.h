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

// Will data transmitted inside a CONNECT packet payload (MQTT 5.0 Section 3.1.3.2).
struct WillData {
    QoS                   qos{QoS::AtMostOnce};
    bool                  retain{false};
    Utf8String            topic;
    BinaryData            payload;
    std::vector<Property> properties;  // Will Properties block

    bool operator==(const WillData&) const noexcept = default;
};

// CONNECT packet (MQTT 5.0 Section 3.1).
// Protocol name ("MQTT") and version (5) are constants — not stored here.
struct ConnectPacket {
    uint16_t                   keep_alive{0};   // seconds; 0 = disabled
    bool                       clean_start{true};
    Utf8String                 client_id;
    std::optional<WillData>    will;
    std::optional<Utf8String>  username;
    std::optional<BinaryData>  password;
    std::vector<Property>      properties;      // Connect Properties block

    bool operator==(const ConnectPacket&) const noexcept = default;
};

// CONNACK packet (MQTT 5.0 Section 3.2).
struct ConnackPacket {
    bool                  session_present{false};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const ConnackPacket&) const noexcept = default;
};

} // namespace mqtt
