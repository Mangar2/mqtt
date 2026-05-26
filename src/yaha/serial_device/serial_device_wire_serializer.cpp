#include "yaha/serial_device/serial_device_wire_serializer.h"

#include <stdexcept>
#include <string>

namespace yaha {
namespace {

constexpr std::int64_t k_max_switch_bit_index{8};

[[nodiscard]] std::string endpointToString(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::to_string(std::get<std::int64_t>(endpointValue));
    }
    if (std::holds_alternative<std::string>(endpointValue)) {
        return std::get<std::string>(endpointValue);
    }
    return "";
}

[[nodiscard]] std::string valueToString(const SerialDeviceValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return std::get<std::string>(value);
}

[[nodiscard]] std::int64_t valueToInt64(const SerialDeviceValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::get<std::int64_t>(value);
    }

    const auto& stringValue = std::get<std::string>(value);
    std::size_t parsedLength{0U};
    const std::int64_t numericValue = std::stoll(stringValue, &parsedLength);
    if (parsedLength != stringValue.size()) {
        throw std::invalid_argument{"value is not fully numeric"};
    }
    return numericValue;
}

[[nodiscard]] std::string escapeJsonString(const std::string& inputText) {
    std::string outputText{};
    outputText.reserve(inputText.size());

    for (const char characterValue : inputText) {
        if (characterValue == '"' || characterValue == '\\') {
            outputText.push_back('\\');
            outputText.push_back(characterValue);
            continue;
        }
        outputText.push_back(characterValue);
    }

    return outputText;
}

[[nodiscard]] std::string endpointToJsonText(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::monostate>(endpointValue)) {
        return "null";
    }
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::to_string(std::get<std::int64_t>(endpointValue));
    }
    return "\"" + escapeJsonString(std::get<std::string>(endpointValue)) + "\"";
}

[[nodiscard]] std::string valueToJsonText(const SerialDeviceValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return "\"" + escapeJsonString(std::get<std::string>(value)) + "\"";
}

[[nodiscard]] std::int64_t leastSignificantBitIndex(const std::int64_t value) {
    if (value <= 0) {
        return -1;
    }

    std::int64_t currentValue = value;
    std::int64_t indexValue = 0;
    while ((currentValue & 1) == 0) {
        currentValue >>= 1;
        ++indexValue;
    }
    return indexValue;
}

[[nodiscard]] std::string serializeSwitchMessage(const SerialDeviceMessage& messageValue) {
    const std::int64_t encodedValue = valueToInt64(messageValue.value);
    const std::int64_t indexValue = leastSignificantBitIndex(encodedValue);
    if (indexValue < 0 || indexValue > k_max_switch_bit_index) {
        return "";
    }

    std::string payload = "s" + std::to_string(indexValue);
    if ((encodedValue & static_cast<std::int64_t>(k_serialdevice_switch_on)) != 0) {
        payload += "H";
    } else if ((encodedValue & static_cast<std::int64_t>(k_serialdevice_switch_off)) != 0) {
        payload += "L";
    }
    return payload;
}

[[nodiscard]] std::string serializeI2cMessage(const SerialDeviceMessage& messageValue) {
    return "C" + endpointToString(messageValue.receiver) + messageValue.command + valueToString(messageValue.value);
}

[[nodiscard]] std::string serializeSerialMessage(const SerialDeviceMessage& messageValue) {
    return std::string{R"({"S":)"} + endpointToJsonText(messageValue.sender) +
           R"(,"R":)" + endpointToJsonText(messageValue.receiver) +
           R"(,"C":")" + escapeJsonString(messageValue.command) +
           R"(","V":)" + valueToJsonText(messageValue.value) + "}";
}

[[nodiscard]] std::string serializeFs20Message(const SerialDeviceMessage& messageValue) {
    const std::size_t delimiterPosition = messageValue.command.find('/');
    const std::string commandPart = delimiterPosition == std::string::npos
        ? ""
        : messageValue.command.substr(delimiterPosition + 1U);

    std::string payload = "G" + commandPart;
    if (std::holds_alternative<std::int64_t>(messageValue.value)) {
        payload += std::to_string(std::get<std::int64_t>(messageValue.value));
    } else {
        payload += std::get<std::string>(messageValue.value);
    }
    return payload;
}

} // namespace

std::string serialDeviceMessageToWireString(const SerialDeviceMessage& messageValue) {
    try {
        if (messageValue.interfaceName == "switch") {
            return serializeSwitchMessage(messageValue);
        }
        if (messageValue.interfaceName == "i2c") {
            return serializeI2cMessage(messageValue);
        }
        if (messageValue.interfaceName == "serial") {
            return serializeSerialMessage(messageValue);
        }
        if (messageValue.interfaceName == "fs20") {
            return serializeFs20Message(messageValue);
        }
    } catch (const std::exception&) {
        return "";
    }

    return "";
}

} // namespace yaha
