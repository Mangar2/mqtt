#pragma once

/**
 * @file utf8_string.h
 * @brief MQTT 5.0 UTF-8 string types (Sections 1.5.4 / 1.5.7).
 */

#include <cstddef>
#include <string>

namespace mqtt {

/**
 * @brief MQTT 5.0 UTF-8 Encoded String (Section 1.5.4).
 *
 * On the wire: 2-byte big-endian length prefix followed by UTF-8 bytes.
 * Maximum length is 65 535 bytes (enforced by the codec module).
 */
struct Utf8String {
    std::string value;

    static constexpr std::size_t k_max_byte_length = 65535U;  ///< Maximum encoded byte length.

    bool operator==(const Utf8String&) const noexcept = default;
};

/**
 * @brief MQTT 5.0 UTF-8 String Pair (Section 1.5.7).
 *
 * Used exclusively for the User Property (property ID 0x26).
 */
struct Utf8StringPair {
    Utf8String name;   ///< Property name.
    Utf8String value;  ///< Property value.

    bool operator==(const Utf8StringPair&) const noexcept = default;
};

} // namespace mqtt
