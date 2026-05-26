#pragma once

/**
 * @file serial_device_wire_serializer.h
 * @brief SerialDevice wire-format serializer with legacy parity rules.
 */

#include "yaha/serial_device/serial_device_message.h"

#include <cstdint>
#include <string>

namespace yaha {

inline constexpr std::uint16_t k_serialdevice_switch_on{0x4000U};
inline constexpr std::uint16_t k_serialdevice_switch_off{0x2000U};

/**
 * @brief Serializes one internal SerialDevice message to a wire payload.
 * @param messageValue Internal message representation.
 * @return Wire payload string, or empty string for unsupported/invalid values.
 */
[[nodiscard]] std::string serialDeviceMessageToWireString(const SerialDeviceMessage& messageValue);

} // namespace yaha
