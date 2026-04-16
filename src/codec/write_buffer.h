#pragma once

/**
 * @file write_buffer.h
 * @brief WriteBuffer type alias for MQTT codec encode operations.
 */

#include <cstdint>
#include <vector>

namespace mqtt {

/**
 * @brief Byte buffer for serialised MQTT data.
 *
 * Encode functions append bytes to a WriteBuffer via `push_back` and `insert`.
 * Callers own the buffer and may pre-reserve capacity.
 */
using WriteBuffer = std::vector<uint8_t>;

} // namespace mqtt
