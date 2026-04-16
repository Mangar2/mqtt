#pragma once

#include <cstdint>

namespace mqtt {

// MQTT 5.0 Control Packet types (Section 2.1.2, Table 2-1).
// Wire values 1–15; value 0 and 16–255 are reserved by the spec.
// Will is not a wire-level packet type — it is used internally to identify
// will-specific properties in the property-to-packet mapping.
enum class PacketType : uint8_t {
    Connect     =  1,
    Connack     =  2,
    Publish     =  3,
    Puback      =  4,
    Pubrec      =  5,
    Pubrel      =  6,
    Pubcomp     =  7,
    Subscribe   =  8,
    Suback      =  9,
    Unsubscribe = 10,
    Unsuback    = 11,
    Pingreq     = 12,
    Pingresp    = 13,
    Disconnect  = 14,
    Auth        = 15,
    Will        = 16,  // Internal: represents will-property context within CONNECT
};

} // namespace mqtt
