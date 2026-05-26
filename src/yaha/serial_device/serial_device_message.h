#pragma once

/**
 * @file serial_device_message.h
 * @brief Internal SerialDevice message model with legacy-compatible value types.
 */

#include <cstdint>
#include <string>
#include <variant>

namespace yaha {

/**
 * @brief Sender/receiver endpoint type used by legacy SerialDevice logic.
 */
using SerialDeviceEndpoint = std::variant<std::monostate, std::int64_t, std::string>;

/**
 * @brief Message value type used by legacy SerialDevice logic.
 */
using SerialDeviceValue = std::variant<std::int64_t, std::string>;

/**
 * @brief Internal serial message representation matching legacy semantics.
 */
struct SerialDeviceMessage {
    std::string interfaceName{};
    SerialDeviceEndpoint sender{};
    SerialDeviceEndpoint receiver{};
    std::string command{};
    SerialDeviceValue value{0};
    std::string action{};

    /**
     * @brief Legacy-like debug string representation.
     * @return Formatted message text.
     */
    [[nodiscard]] std::string toString() const;

    /**
     * @brief Structural equality helper for tests and mapping assertions.
     * @param otherValue Other message value.
     * @return True when all fields are equal.
     */
    [[nodiscard]] bool operator==(const SerialDeviceMessage& otherValue) const = default;
};

} // namespace yaha
