#include "yaha/serial_device/serial_device_mqtt_to_serial_mapper.h"

#include "helper/string_helper.h"
#include "yaha/serial_device/serial_device_wire_serializer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace yaha {
namespace {

constexpr std::int64_t k_serial_value_max{0xFFFF};

[[nodiscard]] bool endsWithIgnoreCase(const std::string& inputValue, const std::string& suffixValue) {
    if (suffixValue.size() > inputValue.size()) {
        return false;
    }

    const std::string inputTail = inputValue.substr(inputValue.size() - suffixValue.size());
    return mqtt::helper::toLower(inputTail) == mqtt::helper::toLower(suffixValue);
}

[[nodiscard]] bool startsWithIgnoreCase(const std::string& inputValue, const std::string& prefixValue) {
    if (prefixValue.size() > inputValue.size()) {
        return false;
    }

    const std::string inputHead = inputValue.substr(0U, prefixValue.size());
    return mqtt::helper::toLower(inputHead) == mqtt::helper::toLower(prefixValue);
}

[[nodiscard]] std::optional<std::int64_t> tryParseInteger(const std::string& valueText) {
    try {
        std::size_t parsedLength{0U};
        const std::int64_t numericValue = std::stoll(valueText, &parsedLength, 10);
        if (parsedLength != valueText.size()) {
            return std::nullopt;
        }
        return numericValue;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] SerialDeviceEndpoint toEndpoint(const std::string& valueText) {
    if (const auto numericValue = tryParseInteger(valueText); numericValue.has_value()) {
        return SerialDeviceEndpoint{numericValue.value()};
    }
    return SerialDeviceEndpoint{valueText};
}

[[nodiscard]] bool valueMapUsesCommand(
    const SerialDeviceValueMapDefinition& definition,
    const std::string& commandValue) {
    return std::ranges::find(definition.usedBy, commandValue) != definition.usedBy.end();
}

[[nodiscard]] std::optional<std::int64_t> tryMapValue(
    const std::unordered_map<std::string, SerialDeviceValueMapDefinition>& valueMap,
    const std::string& commandValue,
    const std::string& inputValue) {
    for (const auto& mapEntry : valueMap) {
        const SerialDeviceValueMapDefinition& definition = mapEntry.second;
        if (!valueMapUsesCommand(definition, commandValue)) {
            continue;
        }

        const std::string loweredInput = mqtt::helper::toLower(inputValue);
        for (const auto& candidateEntry : definition.map) {
            if (mqtt::helper::toLower(candidateEntry.first) == loweredInput) {
                return static_cast<std::int64_t>(candidateEntry.second);
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> tryFallbackBooleanMap(const std::string& inputValue) {
    const std::string loweredInput = mqtt::helper::toLower(inputValue);
    if (loweredInput == "on" || loweredInput == "true" || loweredInput == "1") {
        return 1;
    }
    if (loweredInput == "off" || loweredInput == "false" || loweredInput == "0") {
        return 0;
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t toSerialValue(
    const SerialDeviceInterfaceDefinition& interfaceDefinition,
    const std::string& commandValue,
    const std::string& mqttValue) {
    if (const auto numericValue = tryParseInteger(mqttValue); numericValue.has_value()) {
        if (numericValue.value() < 0 || numericValue.value() > k_serial_value_max) {
            throw std::runtime_error{"The provided value is outside the valid range: " + mqttValue};
        }
        return numericValue.value();
    }

    if (const auto mappedValue = tryMapValue(interfaceDefinition.valueMap, commandValue, mqttValue); mappedValue.has_value()) {
        return mappedValue.value();
    }

    if (interfaceDefinition.valueMap.empty()) {
        if (const auto fallbackValue = tryFallbackBooleanMap(mqttValue); fallbackValue.has_value()) {
            return fallbackValue.value();
        }
    }

    if (const auto literalValue = tryFallbackBooleanMap(mqttValue); literalValue.has_value()) {
        return literalValue.value();
    }

    throw std::runtime_error{"The provided value is not an integer: " + mqttValue};
}

[[nodiscard]] std::optional<SerialDeviceMessage> tryTopicMapRoute(
    const std::string& interfaceName,
    const SerialDeviceInterfaceDefinition& interfaceDefinition,
    const std::string& topicValue,
    const std::string& mqttValue) {
    const auto topicIterator = interfaceDefinition.topicMap.find(topicValue);
    if (topicIterator == interfaceDefinition.topicMap.end()) {
        return std::nullopt;
    }

    const SerialDeviceSwitchTopicMapping& topicMapping = topicIterator->second;
    auto encodedValue = static_cast<std::int64_t>(topicMapping.value);
    if (mqttValue == "on" || mqttValue == "1") {
        encodedValue += static_cast<std::int64_t>(k_serialdevice_switch_on);
    } else {
        encodedValue += static_cast<std::int64_t>(k_serialdevice_switch_off);
    }

    return SerialDeviceMessage{
        .interfaceName = interfaceName,
        .sender = SerialDeviceEndpoint{},
        .receiver = SerialDeviceEndpoint{topicMapping.address},
        .command = topicMapping.command,
        .value = SerialDeviceValue{encodedValue},
        .action = ""};
}

struct InterfaceCommandMatch {
    std::string interfaceName{};
    std::string command{};
    const SerialDeviceInterfaceDefinition* interfaceDefinition{nullptr};
};

[[nodiscard]] std::optional<InterfaceCommandMatch> resolveInterfaceCommandByTopic(
    const SerialDeviceConfig& configValue,
    const std::string& topicValue) {
    for (const auto& interfaceEntry : configValue.interfaces) {
        const std::string& interfaceName = interfaceEntry.first;
        const SerialDeviceInterfaceDefinition& interfaceDefinition = interfaceEntry.second;

        for (const auto& commandEntry : interfaceDefinition.commandMap) {
            if (endsWithIgnoreCase(topicValue, commandEntry.second)) {
                return InterfaceCommandMatch{
                    .interfaceName = interfaceName,
                    .command = commandEntry.first,
                    .interfaceDefinition = &interfaceDefinition};
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] SerialDeviceEndpoint resolveReceiverByTopic(
    const std::unordered_map<std::string, std::string>& receiverMap,
    const std::string& topicValue) {
    for (const auto& receiverEntry : receiverMap) {
        if (startsWithIgnoreCase(topicValue, receiverEntry.first)) {
            return toEndpoint(receiverEntry.second);
        }
    }

    return SerialDeviceEndpoint{std::string{}};
}

} // namespace

SerialDeviceMessage mapMqttToSerialMessage(
    const SerialDeviceConfig& configValue,
    const std::string& topicValue,
    const std::string& valueValue) {
    for (const auto& interfaceEntry : configValue.interfaces) {
        const auto mappedByTopic = tryTopicMapRoute(interfaceEntry.first, interfaceEntry.second, topicValue, valueValue);
        if (mappedByTopic.has_value()) {
            return mappedByTopic.value();
        }
    }

    const auto commandMatch = resolveInterfaceCommandByTopic(configValue, topicValue);
    if (!commandMatch.has_value() || commandMatch->interfaceDefinition == nullptr) {
        throw std::runtime_error{"undefined device setting " + topicValue};
    }

    const SerialDeviceInterfaceDefinition& interfaceDefinition = *commandMatch->interfaceDefinition;

    return SerialDeviceMessage{
        .interfaceName = commandMatch->interfaceName,
        .sender = SerialDeviceEndpoint{},
        .receiver = resolveReceiverByTopic(interfaceDefinition.receiverMap, topicValue),
        .command = commandMatch->command,
        .value = SerialDeviceValue{toSerialValue(interfaceDefinition, commandMatch->command, valueValue)},
        .action = ""};
}

} // namespace yaha
