#pragma once

/**
 * @file publish_packets.h
 * @brief MQTT 5.0 PUBLISH and QoS acknowledgement packet structs (Sections 3.3–3.7).
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
 * @brief PUBLISH packet (Section 3.3).
 */
struct PublishPacket {
    bool                       dup{false};            ///< Duplicate delivery flag (QoS > 0 only).
    QoS                        qos{QoS::AtMostOnce};  ///< Delivery quality of service.
    bool                       retain{false};          ///< Retain flag.
    Utf8String                 topic;                  ///< Topic name.
    std::optional<uint16_t>    packet_id;              ///< Packet identifier; present iff qos > AtMostOnce.
    BinaryData                 payload;                ///< Application message payload.
    std::vector<Property>      properties;             ///< Publish Properties block.

    bool operator==(const PublishPacket&) const noexcept = default;
};

/**
 * @brief PUBACK packet (Section 3.4) — QoS 1 acknowledgement.
 */
struct PubackPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubackPacket&) const noexcept = default;
};

/**
 * @brief PUBREC packet (Section 3.5) — QoS 2 step 1 response.
 */
struct PubrecPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubrecPacket&) const noexcept = default;
};

/**
 * @brief PUBREL packet (Section 3.6) — QoS 2 step 2 release.
 */
struct PubrelPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubrelPacket&) const noexcept = default;
};

/**
 * @brief PUBCOMP packet (Section 3.7) — QoS 2 step 3 complete.
 */
struct PubcompPacket {
    uint16_t              packet_id{0};
    ReasonCode            reason_code{ReasonCode::Success};
    std::vector<Property> properties;

    bool operator==(const PubcompPacket&) const noexcept = default;
};

} // namespace mqtt
