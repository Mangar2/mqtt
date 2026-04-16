#pragma once

#include <cstdint>

namespace mqtt {

// MQTT 5.0 Two Byte Integer (Section 1.5.2).
// Big-endian unsigned 16-bit integer. Encoding/decoding handled by the codec module.
using TwoByteInteger = uint16_t;

// MQTT 5.0 Four Byte Integer (Section 1.5.3).
// Big-endian unsigned 32-bit integer. Encoding/decoding handled by the codec module.
using FourByteInteger = uint32_t;

} // namespace mqtt
