#include "yaha/serial_device/serial_device_message.h"

#include <cstddef>
#include <exception>

namespace yaha {
namespace {

[[nodiscard]] std::string valueToString(const SerialDeviceValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return std::get<std::string>(value);
}

} // namespace

std::string endpointToString(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::monostate>(endpointValue)) {
        return "null";
    }
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::to_string(std::get<std::int64_t>(endpointValue));
    }
    return std::get<std::string>(endpointValue);
}

std::optional<std::int64_t> endpointToNumeric(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::get<std::int64_t>(endpointValue);
    }
    if (std::holds_alternative<std::string>(endpointValue)) {
        try {
            const auto& textValue = std::get<std::string>(endpointValue);
            std::size_t parsedLength{0U};
            const std::int64_t numericValue = std::stoll(textValue, &parsedLength, 10);
            if (parsedLength == textValue.size()) {
                return numericValue;
            }
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

mqtt::json::JsonValue endpointToJsonValue(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::monostate>(endpointValue)) {
        return mqtt::json::JsonValue{};
    }
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return mqtt::json::JsonValue{static_cast<double>(std::get<std::int64_t>(endpointValue))};
    }
    return mqtt::json::JsonValue{std::get<std::string>(endpointValue)};
}

std::string SerialDeviceMessage::toString() const {
    return "Interface: " + interfaceName +
           " Sender: " + endpointToString(sender) +
           " Receiver: " + endpointToString(receiver) +
           " Command: " + command +
           " Value: " + valueToString(value);
}

} // namespace yaha
