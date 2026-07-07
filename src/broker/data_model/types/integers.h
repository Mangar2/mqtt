#pragma once

/**
 * @file integers.h
 * @brief MQTT 5.0 fixed-width integer type aliases (Sections 1.5.2 / 1.5.3).
 */

#include <cstdint>

namespace mqtt {

/** @brief MQTT 5.0 Two Byte Integer (Section 1.5.2). Big-endian unsigned 16-bit integer. */
using TwoByteInteger = uint16_t;

/** @brief MQTT 5.0 Four Byte Integer (Section 1.5.3). Big-endian unsigned 32-bit integer. */
using FourByteInteger = uint32_t;

} // namespace mqtt
