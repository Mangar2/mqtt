#pragma once

#include <cstdint>

namespace mqtt {

// MQTT 5.0 Variable Byte Integer (Section 1.5.5).
// Encodes unsigned integers in the range [0, 268 435 455] using 1–4 bytes on the wire.
// Encoding/decoding is performed by the codec module (2.1.1 / 2.1.2).
struct VariableByteInteger {
    uint32_t value{0};

    static constexpr uint32_t k_max_value = 268'435'455U;

    // Returns the number of bytes required to encode this value on the wire (1–4).
    [[nodiscard]] constexpr uint8_t encoded_size() const noexcept
    {
        if (value < 128U)       return 1;
        if (value < 16'384U)    return 2;
        if (value < 2'097'152U) return 3;
        return 4;
    }

    constexpr bool operator==(const VariableByteInteger&) const noexcept = default;
};

} // namespace mqtt
