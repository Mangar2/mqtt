#pragma once

/**
 * @file binary_data.h
 * @brief MQTT 5.0 Binary Data type (Section 1.5.6).
 */

#include <cstddef>
#include <cstdint>
#include <string_view>
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

    /**
     * @brief Convert text bytes 1:1 into BinaryData.
     *
     * @param text Source text.
     * @return BinaryData containing the byte values from @p text.
     */
    [[nodiscard]] static BinaryData from_string(std::string_view text) {
        BinaryData binary_data;
        binary_data.data.reserve(text.size());
        for (char character : text) {
            binary_data.data.push_back(static_cast<uint8_t>(character));
        }
        return binary_data;
    }

    bool operator==(const BinaryData&) const noexcept = default;
};

} // namespace mqtt
