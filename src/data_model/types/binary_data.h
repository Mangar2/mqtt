#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mqtt {

// MQTT 5.0 Binary Data (Section 1.5.6).
// On the wire: 2-byte big-endian length prefix followed by raw bytes.
// Maximum length is 65 535 bytes (enforced by the codec module).
struct BinaryData {
    std::vector<uint8_t> data;

    static constexpr std::size_t k_max_byte_length = 65535U;

    bool operator==(const BinaryData&) const noexcept = default;
};

} // namespace mqtt
