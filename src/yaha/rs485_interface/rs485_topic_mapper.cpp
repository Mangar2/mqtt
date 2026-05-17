#include "yaha/rs485_interface/rs485_topic_mapper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr std::uint16_t k_switch_on{0x4000U};
constexpr std::uint16_t k_switch_off{0x2000U};
constexpr double k_u16_max_as_double{65535.0};
constexpr double k_integer_epsilon{1e-9};

[[nodiscard]] std::string toLowerCopy(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char characterValue) {
        return static_cast<char>(std::tolower(characterValue));
    });
    return text;
}

[[nodiscard]] bool startsWithCaseInsensitive(const std::string& text, const std::string& prefix) {
    if (prefix.size() > text.size()) {
        return false;
    }

    return toLowerCopy(text.substr(0U, prefix.size())) == toLowerCopy(prefix);
}

[[nodiscard]] bool endsWithCaseInsensitive(const std::string& text, const std::string& suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }

    return toLowerCopy(text.substr(text.size() - suffix.size())) == toLowerCopy(suffix);
}

[[nodiscard]] bool isOnPayload(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text == "on" || *text == "1";
    }

    const auto numeric = std::get<double>(value);
    return std::fabs(numeric - 1.0) < k_integer_epsilon;
}

[[nodiscard]] std::uint16_t requireUInt16Integer(const double number, const std::string& originalValueText) {
    if (!std::isfinite(number)) {
        throw std::runtime_error("The provided value is not an integer: " + originalValueText);
    }

    const double rounded = std::round(number);
    if (std::fabs(number - rounded) > k_integer_epsilon) {
        throw std::runtime_error("The provided value is not an integer: " + originalValueText);
    }

    if (rounded < 0.0 || rounded > k_u16_max_as_double) {
        throw std::runtime_error(
            "The provided value is not a positive two byte value; 0 <= value <= 0xFFFF: " +
            originalValueText);
    }

    return static_cast<std::uint16_t>(rounded);
}

[[nodiscard]] std::string valueToText(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text;
    }

    return std::format("{}", std::get<double>(value));
}

} // namespace

Rs485TopicMapper::Rs485TopicMapper(Rs485InterfaceConfig config)
    : config_(std::move(config)) {}

Rs485MappedSerialData Rs485TopicMapper::toSerialData(const Message& mqttMessage) const {
    const auto explicitTopicIterator = config_.topics.find(mqttMessage.topic());
    if (explicitTopicIterator != config_.topics.end()) {
        Rs485MappedSerialData mapped{};
        mapped.address = explicitTopicIterator->second.address;
        mapped.command = explicitTopicIterator->second.command;
        mapped.value = explicitTopicIterator->second.value;
        mapped.value = static_cast<std::uint16_t>(
            mapped.value + (isOnPayload(mqttMessage.value()) ? k_switch_on : k_switch_off));
        return mapped;
    }

    Rs485MappedSerialData mapped{};
    mapped.address = resolveAddressByTopic(mqttMessage.topic());
    mapped.command = resolveCommandByTopic(mqttMessage.topic());
    mapped.value = resolveValueByCommandAndPayload(mapped.command, mqttMessage.value());
    return mapped;
}

std::vector<Message> Rs485TopicMapper::toMqttMessages(const Rs485SerialMessage& serialMessage) const {
    std::vector<Message> mapped{};

    for (const auto& [topic, rule] : config_.topics) {
        if (rule.command != serialMessage.command || rule.address != serialMessage.sender) {
            continue;
        }

        const auto serialValue = static_cast<std::uint16_t>(serialMessage.value);
        const bool isSwitchOnMessage = (serialValue & k_switch_on) != 0U;
        const bool isSwitchOffMessage = (serialValue & k_switch_off) != 0U;
        const bool isSwitchMessage = isSwitchOnMessage || isSwitchOffMessage;
        const bool bitIsSet = (serialValue & rule.value) != 0U;

        if (!isSwitchMessage) {
            mapped.emplace_back(topic, bitIsSet ? std::string{"on"} : std::string{"off"});
            continue;
        }

        if (bitIsSet) {
            mapped.emplace_back(topic, isSwitchOffMessage ? std::string{"off"} : std::string{"on"});
        }
    }

    if (!mapped.empty()) {
        return mapped;
    }

    const std::string topicPrefix = resolveTopicPrefixByAddress(serialMessage.sender);
    const std::string topicSuffix = resolveTopicSuffixByCommand(serialMessage.command);
    const Value value = resolveMqttValueByCommand(serialMessage.command, serialMessage.value);
    mapped.emplace_back(topicPrefix + topicSuffix, value);
    return mapped;
}

std::uint8_t Rs485TopicMapper::resolveAddressByTopic(const std::string& topic) const {
    std::size_t bestLength{0U};
    bool hasMatch{false};
    std::uint8_t resolvedAddress{0U};

    for (const auto& [prefix, address] : config_.addresses) {
        if (!startsWithCaseInsensitive(topic, prefix)) {
            continue;
        }

        if (!hasMatch || prefix.size() > bestLength) {
            hasMatch = true;
            bestLength = prefix.size();
            resolvedAddress = address;
        }
    }

    if (!hasMatch) {
        throw std::runtime_error("undefined device address " + topic);
    }

    return resolvedAddress;
}

char Rs485TopicMapper::resolveCommandByTopic(const std::string& topic) const {
    for (const auto& [command, suffix] : config_.settings) {
        if (endsWithCaseInsensitive(topic, suffix)) {
            return command;
        }
    }

    throw std::runtime_error("undefined device setting " + topic);
}

std::uint16_t Rs485TopicMapper::resolveValueByCommandAndPayload(
    const char command,
    const Value& mqttValue) const {
    if (const auto* numeric = std::get_if<double>(&mqttValue); numeric != nullptr) {
        return requireUInt16Integer(*numeric, valueToText(mqttValue));
    }

    const std::string textValue = std::get<std::string>(mqttValue);

    char* endPointer = nullptr;
    const double parsedNumber = std::strtod(textValue.c_str(), &endPointer);
    if (endPointer != nullptr && *endPointer == '\0') {
        return requireUInt16Integer(parsedNumber, textValue);
    }

    const std::string normalizedValue = toLowerCopy(textValue);
    for (const auto& [interfaceName, definition] : config_.interfaces) {
        (void)interfaceName;
        const bool commandUsedByInterface =
            std::ranges::find(definition.usedBy, command) != definition.usedBy.end();
        if (!commandUsedByInterface) {
            continue;
        }

        const auto mappedValueIterator = definition.map.find(normalizedValue);
        if (mappedValueIterator != definition.map.end()) {
            return mappedValueIterator->second;
        }
    }

    throw std::runtime_error("The provided value is not an integer: " + textValue);
}

std::string Rs485TopicMapper::resolveTopicPrefixByAddress(const std::uint8_t serialAddress) const {
    for (const auto& [topicPrefix, address] : config_.addresses) {
        if (address == serialAddress) {
            return topicPrefix;
        }
    }

    throw std::runtime_error(std::format("Unknown serial address: {}", serialAddress));
}

std::string Rs485TopicMapper::resolveTopicSuffixByCommand(const char command) const {
    const auto settingsIterator = config_.settings.find(command);
    if (settingsIterator != config_.settings.end()) {
        return settingsIterator->second;
    }

    const auto statusIterator = config_.status.find(command);
    if (statusIterator != config_.status.end()) {
        return statusIterator->second;
    }

    throw std::runtime_error(std::format("Unknown serial command: {}", command));
}

Value Rs485TopicMapper::resolveMqttValueByCommand(const char command, const double serialValue) const {
    for (const auto& [interfaceName, definition] : config_.interfaces) {
        (void)interfaceName;
        const bool commandUsedByInterface =
            std::ranges::find(definition.usedBy, command) != definition.usedBy.end();
        if (!commandUsedByInterface) {
            continue;
        }

        for (const auto& [key, mappedValue] : definition.map) {
            if (std::fabs(static_cast<double>(mappedValue) - serialValue) < k_integer_epsilon) {
                return key;
            }
        }
    }

    return serialValue;
}

} // namespace yaha
