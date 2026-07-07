#pragma once

/**
 * @file connect_packet.h
 * @brief MQTT 5.0 CONNECT and CONNACK packet structs (Sections 3.1 / 3.2).
 */

#include <cstdint>
#include <optional>
#include <vector>
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

/**
 * @brief Will data transmitted inside a CONNECT packet payload (Section 3.1.3.2).
 */
struct WillData {
    QoS                   qos{QoS::AtMostOnce};  ///< Will QoS level.
    bool                  retain{false};           ///< Will retain flag.
    Utf8String            topic;                   ///< Will topic.
    BinaryData            payload;                 ///< Will payload.
    std::vector<Property> properties;              ///< Will Properties block.

    bool operator==(const WillData&) const noexcept = default;
};

/**
 * @brief CONNECT packet (Section 3.1).
 *
 * Protocol name ("MQTT") and protocol version (5) are constants and are not stored here.
 */
struct ConnectPacket {
    uint16_t                   keep_alive{0};    ///< Keep-alive in seconds; 0 disables keep-alive.
    bool                       clean_start{true};///< When true, discard any existing session.
    Utf8String                 client_id;         ///< Client identifier.
    std::optional<WillData>    will;              ///< Will message; absent when no will is set.
    std::optional<Utf8String>  username;          ///< Optional username credential.
    std::optional<BinaryData>  password;          ///< Optional password credential.
    std::vector<Property>      properties;        ///< Connect Properties block.

    bool operator==(const ConnectPacket&) const noexcept = default;
};

/**
 * @brief CONNACK packet (Section 3.2).
 */
struct ConnackPacket {
    bool                  session_present{false};           ///< True if a previous session was resumed.
    ReasonCode            reason_code{ReasonCode::Success}; ///< Connection result.
    std::vector<Property> properties;                       ///< Connack Properties block.

    bool operator==(const ConnackPacket&) const noexcept = default;
};

} // namespace mqtt
