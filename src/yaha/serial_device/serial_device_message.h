#pragma once

/**
 * @file serial_device_message.h
 * @brief Internal SerialDevice message model with legacy-compatible value types.
 */

#include "json/json_value.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace yaha {

/**
 * @brief Sender/receiver endpoint type used by legacy SerialDevice logic.
 */
using SerialDeviceEndpoint = std::variant<std::monostate, std::int64_t, std::string>;

/**
 * @brief Converts a SerialDevice endpoint to its text representation.
 * @param endpointValue Endpoint value to convert.
 * @return The numeric text or raw string value, or "null" when unset.
 */
[[nodiscard]] std::string endpointToString(const SerialDeviceEndpoint& endpointValue);

/**
 * @brief Converts a SerialDevice endpoint to a numeric address when possible.
 * @param endpointValue Endpoint value to convert.
 * @return Parsed integer value, or std::nullopt when the endpoint is not fully numeric.
 */
[[nodiscard]] std::optional<std::int64_t> endpointToNumeric(const SerialDeviceEndpoint& endpointValue);

/**
 * @brief Converts a SerialDevice endpoint to its JSON representation.
 * @param endpointValue Endpoint value to convert.
 * @return JSON null, number, or string value matching the endpoint's active type.
 */
[[nodiscard]] mqtt::json::JsonValue endpointToJsonValue(const SerialDeviceEndpoint& endpointValue);

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
