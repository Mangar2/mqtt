#pragma once

#include <cstddef>
#include <string>

namespace mqtt {

// MQTT 5.0 UTF-8 Encoded String (Section 1.5.4).
// On the wire: 2-byte big-endian length prefix followed by UTF-8 bytes.
// Maximum length is 65 535 bytes (enforced by the codec module).
struct Utf8String {
    std::string value;

    static constexpr std::size_t k_max_byte_length = 65535U;

    bool operator==(const Utf8String&) const noexcept = default;
};

// MQTT 5.0 UTF-8 String Pair (Section 1.5.7).
// Used exclusively for the User Property (property ID 0x26).
struct Utf8StringPair {
    Utf8String name;
    Utf8String value;

    bool operator==(const Utf8StringPair&) const noexcept = default;
};

} // namespace mqtt
