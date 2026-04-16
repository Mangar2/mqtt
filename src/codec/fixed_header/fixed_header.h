#pragma once

/**
 * @file fixed_header.h
 * @brief MQTT 5.0 Fixed Header data structure.
 */

#include <cstdint>
#include "data_model/packet/packet_type.h"

namespace mqtt {

/**
 * @brief Decoded contents of an MQTT 5.0 Fixed Header (Section 2.1).
 *
 * The fixed header occupies 2–5 bytes on the wire:
 *   - Byte 1: `(type << 4) | flags`
 *   - Bytes 2–5: remaining length as a Variable Byte Integer (1–4 bytes)
 */
struct FixedHeader {
    PacketType type;              ///< Control packet type (upper 4 bits of byte 1).
    uint8_t    flags{0};          ///< Control flags (lower 4 bits of byte 1).
    uint32_t   remaining_length{0}; ///< Byte count of the packet after the fixed header.

    bool operator==(const FixedHeader&) const noexcept = default;
};

} // namespace mqtt
