#include "yaha/serial_device/serial_device_message.h"

namespace yaha {
namespace {

[[nodiscard]] std::string endpointToString(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::monostate>(endpointValue)) {
        return "null";
    }
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::to_string(std::get<std::int64_t>(endpointValue));
    }
    return std::get<std::string>(endpointValue);
}

[[nodiscard]] std::string valueToString(const SerialDeviceValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return std::get<std::string>(value);
}

} // namespace

std::string SerialDeviceMessage::toString() const {
    return "Interface: " + interfaceName +
           " Sender: " + endpointToString(sender) +
           " Receiver: " + endpointToString(receiver) +
           " Command: " + command +
           " Value: " + valueToString(value);
}

} // namespace yaha
