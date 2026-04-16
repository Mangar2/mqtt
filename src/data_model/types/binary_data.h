#pragma once

/**
 * @file binary_data.h
 * @brief MQTT 5.0 Binary Data type (Section 1.5.6).
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mqtt {

/**
 * @brief MQTT 5.0 Binary Data.
 *
 * On the wire: 2-byte big-endian length prefix followed by raw bytes.
 * Maximum length is 65 535 bytes (enforced by the codec module).
 */
struct BinaryData {
    std::vector<uint8_t> data;

    static constexpr std::size_t k_max_byte_length = 65535U;  ///< Maximum encoded byte length.

    bool operator==(const BinaryData&) const noexcept = default;
};

} // namespace mqtt
