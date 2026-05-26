#include "yaha/serial_device/serial_device_serial_to_mqtt_mapper.h"

#include "yaha/serial_device/serial_device_wire_serializer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace yaha {
namespace {

[[nodiscard]] std::string endpointToString(const SerialDeviceEndpoint& endpointValue) {
    if (std::holds_alternative<std::monostate>(endpointValue)) {
        return "";
    }
    if (std::holds_alternative<std::int64_t>(endpointValue)) {
        return std::to_string(std::get<std::int64_t>(endpointValue));
    }
    return std::get<std::string>(endpointValue);
}

[[nodiscard]] std::optional<std::int64_t> endpointToNumeric(const SerialDeviceEndpoint& endpointValue) {
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

[[nodiscard]] bool endpointMatchesAddress(
    const SerialDeviceEndpoint& endpointValue,
    const std::string& addressValue) {
    if (endpointToString(endpointValue) == addressValue) {
        return true;
    }

    const auto endpointNumeric = endpointToNumeric(endpointValue);
    if (!endpointNumeric.has_value()) {
        return false;
    }

    try {
        std::size_t parsedLength{0U};
        const std::int64_t addressNumeric = std::stoll(addressValue, &parsedLength, 10);
        return parsedLength == addressValue.size() && endpointNumeric.value() == addressNumeric;
    } catch (const std::exception&) {
        return false;
    }
}

[[nodiscard]] bool commandUsesValueMap(
    const SerialDeviceValueMapDefinition& definition,
    const std::string& commandValue) {
    return std::ranges::find(definition.usedBy, commandValue) != definition.usedBy.end();
}

[[nodiscard]] Value toMqttValue(
    const SerialDeviceInterfaceDefinition& interfaceDefinition,
    const std::string& commandValue,
    const SerialDeviceValue& serialValue) {
    if (std::holds_alternative<std::string>(serialValue)) {
        return std::get<std::string>(serialValue);
    }

    const std::int64_t numericSerialValue = std::get<std::int64_t>(serialValue);

    for (const auto& mapEntry : interfaceDefinition.valueMap) {
        const SerialDeviceValueMapDefinition& definition = mapEntry.second;
        if (!commandUsesValueMap(definition, commandValue)) {
            continue;
        }

        for (const auto& candidateEntry : definition.map) {
            if (static_cast<std::int64_t>(candidateEntry.second) == numericSerialValue) {
                return candidateEntry.first;
            }
        }
    }

    return static_cast<double>(numericSerialValue);
}

[[nodiscard]] std::string resolveTopicPrefix(
    const std::unordered_map<std::string, std::string>& receiverMap,
    const SerialDeviceEndpoint& serialAddress) {
    for (const auto& receiverEntry : receiverMap) {
        if (endpointMatchesAddress(serialAddress, receiverEntry.second)) {
            return receiverEntry.first;
        }
    }

    if (!std::holds_alternative<std::monostate>(serialAddress)) {
        throw std::runtime_error{"Unknown serial address: " + endpointToString(serialAddress)};
    }

    return "";
}

[[nodiscard]] std::string resolveTopicSuffix(
    const SerialDeviceInterfaceDefinition& interfaceDefinition,
    const std::string& serialCommand) {
    if (const auto commandIterator = interfaceDefinition.commandMap.find(serialCommand);
        commandIterator != interfaceDefinition.commandMap.end()) {
        return commandIterator->second;
    }

    if (const auto sendIterator = interfaceDefinition.sendMap.find(serialCommand);
        sendIterator != interfaceDefinition.sendMap.end()) {
        return sendIterator->second;
    }

    throw std::runtime_error{"Unknown serial command: " + serialCommand};
}

[[nodiscard]] Message makeOutputMessage(const std::string& topicValue, Value valueValue) {
    Message output{topicValue, std::move(valueValue), Qos::AtMostOnce};
    output.addReason("received from arduino", "");
    return output;
}

[[nodiscard]] std::vector<Message> mapSwitchMessage(
    const SerialDeviceInterfaceDefinition& interfaceDefinition,
    const SerialDeviceMessage& serialMessage) {
    std::vector<std::pair<std::string, SerialDeviceSwitchTopicMapping>> orderedTopicMap{};
    orderedTopicMap.reserve(interfaceDefinition.topicMap.size());
    for (const auto& entry : interfaceDefinition.topicMap) {
        orderedTopicMap.emplace_back(entry.first, entry.second);
    }

    std::ranges::sort(orderedTopicMap, [](const auto& leftValue, const auto& rightValue) {
        return leftValue.first < rightValue.first;
    });

    const std::int64_t serialValue = std::holds_alternative<std::int64_t>(serialMessage.value)
        ? std::get<std::int64_t>(serialMessage.value)
        : 0;

    const bool isSwitchOnMessage = (serialValue & static_cast<std::int64_t>(k_serialdevice_switch_on)) != 0;
    const bool isSwitchOffMessage = (serialValue & static_cast<std::int64_t>(k_serialdevice_switch_off)) != 0;
    const bool isSwitchMessage = isSwitchOnMessage || isSwitchOffMessage;

    std::vector<Message> outputMessages{};
    for (const auto& entry : orderedTopicMap) {
        const std::string& topicValue = entry.first;
        const SerialDeviceSwitchTopicMapping& required = entry.second;

        if (serialMessage.command != required.command || endpointToString(serialMessage.sender) != required.address) {
            continue;
        }

        const bool bitIsSet = (serialValue & static_cast<std::int64_t>(required.value)) != 0;
        if (!isSwitchMessage) {
            outputMessages.push_back(makeOutputMessage(topicValue, bitIsSet ? Value{std::string{"on"}} : Value{std::string{"off"}}));
            continue;
        }

        if (bitIsSet) {
            outputMessages.push_back(makeOutputMessage(topicValue, isSwitchOffMessage ? Value{std::string{"off"}} : Value{std::string{"on"}}));
        }
    }

    return outputMessages;
}

} // namespace

std::vector<Message> mapSerialMessageToMqttMessages(
    const SerialDeviceConfig& configValue,
    const SerialDeviceMessage& serialMessage) {
    const auto interfaceIterator = configValue.interfaces.find(serialMessage.interfaceName);
    if (interfaceIterator == configValue.interfaces.end()) {
        throw std::runtime_error{"Unknown interface: " + serialMessage.interfaceName};
    }

    const SerialDeviceInterfaceDefinition& interfaceDefinition = interfaceIterator->second;
    if (serialMessage.interfaceName == "switch") {
        return mapSwitchMessage(interfaceDefinition, serialMessage);
    }

    const std::string topicPrefix = resolveTopicPrefix(interfaceDefinition.receiverMap, serialMessage.sender);
    const std::string topicSuffix = resolveTopicSuffix(interfaceDefinition, serialMessage.command);
    const std::string fullTopic = topicPrefix + topicSuffix + serialMessage.action;
    Value outputValue = toMqttValue(interfaceDefinition, serialMessage.command, serialMessage.value);

    std::vector<Message> outputMessages{};
    outputMessages.push_back(makeOutputMessage(fullTopic, std::move(outputValue)));
    return outputMessages;
}

} // namespace yaha
