#pragma once

#include <vector>
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"

namespace mqtt {

// PINGREQ packet (MQTT 5.0 Section 3.12).
// No variable header or payload.
struct PingreqPacket {
    bool operator==(const PingreqPacket&) const noexcept = default;
};

// PINGRESP packet (MQTT 5.0 Section 3.13).
// No variable header or payload.
struct PingrespPacket {
    bool operator==(const PingrespPacket&) const noexcept = default;
};

// DISCONNECT packet (MQTT 5.0 Section 3.14).
struct DisconnectPacket {
    ReasonCode            reason_code{ReasonCode::Success};  // 0x00 = Normal Disconnection
    std::vector<Property> properties;

    bool operator==(const DisconnectPacket&) const noexcept = default;
};

// AUTH packet (MQTT 5.0 Section 3.15).
// Used for enhanced authentication and re-authentication.
struct AuthPacket {
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const AuthPacket&) const noexcept = default;
};

} // namespace mqtt
