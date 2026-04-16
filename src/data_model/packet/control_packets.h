#pragma once

/**
 * @file control_packets.h
 * @brief MQTT 5.0 PINGREQ, PINGRESP, DISCONNECT, and AUTH packet structs (Sections 3.12–3.15).
 */

#include <vector>
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"

namespace mqtt {

/**
 * @brief PINGREQ packet (Section 3.12). No variable header or payload.
 */
struct PingreqPacket {
    bool operator==(const PingreqPacket&) const noexcept = default;
};

/**
 * @brief PINGRESP packet (Section 3.13). No variable header or payload.
 */
struct PingrespPacket {
    bool operator==(const PingrespPacket&) const noexcept = default;
};

/**
 * @brief DISCONNECT packet (Section 3.14).
 */
struct DisconnectPacket {
    ReasonCode            reason_code{ReasonCode::Success};  ///< 0x00 = Normal Disconnection.
    std::vector<Property> properties;

    bool operator==(const DisconnectPacket&) const noexcept = default;
};

/**
 * @brief AUTH packet (Section 3.15).
 *
 * Used for enhanced authentication and re-authentication.
 */
struct AuthPacket {
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const AuthPacket&) const noexcept = default;
};

} // namespace mqtt
