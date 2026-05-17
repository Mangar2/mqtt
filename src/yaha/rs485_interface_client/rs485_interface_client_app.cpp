#include "yaha/rs485_interface_client/rs485_interface_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yaha {
namespace {

[[nodiscard]] std::string trimCopy(std::string value) {
    const auto notSpace = [](const unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::ranges::find_if(value, [&](const char characterValue) {
        return notSpace(static_cast<unsigned char>(characterValue));
    }));

    value.erase(
        std::ranges::find_if(value.rbegin(), value.rend(), [&](const char characterValue) {
            return notSpace(static_cast<unsigned char>(characterValue));
        }).base(),
        value.end());

    return value;
}

[[nodiscard]] std::vector<std::string> split(std::string text, const char delimiter) {
    std::vector<std::string> tokens{};
    std::stringstream stream{std::move(text)};
    std::string part{};
    while (std::getline(stream, part, delimiter)) {
        tokens.push_back(trimCopy(part));
    }
    return tokens;
}

[[nodiscard]] bool requireNonEmptyString(
    const IniDocument& document,
    const std::string_view sectionName,
    const std::string_view keyName,
    std::string& output,
    std::string& errorMessage) {
    const auto value = document.lastValue(sectionName, keyName);
    if (!value.has_value() || trimCopy(*value).empty()) {
        errorMessage = std::format(
            "missing required setting '{}.{}'",
            sectionName,
            keyName);
        return false;
    }

    output = trimCopy(*value);
    return true;
}

[[nodiscard]] bool parseSingleCommandKey(
    const std::string& rawKey,
    const std::string_view sectionName,
    char& output,
    std::string& errorMessage) {
    if (rawKey.size() != 1U) {
        errorMessage = std::format(
            "invalid key '{}' in [{}] (expected single-character command)",
            rawKey,
            sectionName);
        return false;
    }

    output = rawKey.front();
    return true;
}

[[nodiscard]] bool parseTraceLevel(
    const std::string& text,
    std::string& output,
    std::string& errorMessage) {
    if (text == "errors" || text == "messages" || text == "internal") {
        output = text;
        return true;
    }

    errorMessage = std::format(
        "invalid value for rs485interface.trace (expected one of: errors,messages,internal; got '{}')",
        text);
    return false;
}

[[nodiscard]] bool parseTopicsSection(
    const IniDocument& document,
    std::unordered_map<std::string, Rs485TopicMapping>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection("rs485interface.topics");
    if (section == nullptr || section->entries().empty()) {
        return true;
    }

    std::unordered_map<std::string, Rs485TopicMapping> parsed{};
    for (const auto& entry : section->entries()) {
        const std::string topic = trimCopy(entry.key);
        if (topic.empty()) {
            errorMessage = "invalid entry in [rs485interface.topics] (topic key must not be empty)";
            return false;
        }

        const auto parts = split(entry.value, ',');
        if (parts.size() != 3U) {
            errorMessage = std::format(
                "invalid mapping for topic '{}' in [rs485interface.topics] "
                "(expected format COMMAND,VALUE,ADDRESS)",
                topic);
            return false;
        }

        if (parts[0].size() != 1U) {
            errorMessage = std::format(
                "invalid command for topic '{}' in [rs485interface.topics] "
                "(expected single-character command)",
                topic);
            return false;
        }

        const auto value = IniDocument::parseUnsigned(parts[1], 0U, 65535U);
        if (!value.has_value()) {
            errorMessage = std::format(
                "invalid value for topic '{}' in [rs485interface.topics] "
                "(expected unsigned 0..65535)",
                topic);
            return false;
        }

        const auto address = IniDocument::parseUnsigned(parts[2], 1U, 127U);
        if (!address.has_value()) {
            errorMessage = std::format(
                "invalid address for topic '{}' in [rs485interface.topics] "
                "(expected unsigned 1..127)",
                topic);
            return false;
        }

        parsed[topic] = Rs485TopicMapping{
            .command = parts[0].front(),
            .value = static_cast<std::uint16_t>(*value),
            .address = static_cast<std::uint8_t>(*address)};
    }

    output = std::move(parsed);
    return true;
}

struct ParsedInterfaceSegments {
    std::string usedByText{};
    std::string mapText{};
};

[[nodiscard]] ParsedInterfaceSegments extractInterfaceSegments(const std::string& rawValue) {
    ParsedInterfaceSegments parsed{};
    const auto segments = split(rawValue, ';');
    for (const auto& segment : segments) {
        const auto equalPos = segment.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }
        const std::string name = trimCopy(segment.substr(0U, equalPos));
        const std::string value = trimCopy(segment.substr(equalPos + 1U));
        if (name == "usedby") {
            parsed.usedByText = value;
        } else if (name == "map") {
            parsed.mapText = value;
        }
    }
    return parsed;
}

[[nodiscard]] bool parseInterfaceUsedByTokens(
    const std::string& interfaceName,
    const std::string& usedByText,
    Rs485InterfaceDefinition& output,
    std::string& errorMessage) {
    const auto usedByTokens = split(usedByText, ',');
    for (const auto& token : usedByTokens) {
        if (token.size() != 1U) {
            errorMessage = std::format(
                "invalid usedby token '{}' for interface '{}' in [rs485interface.interfaces] "
                "(expected single character)",
                token,
                interfaceName);
            return false;
        }
        output.usedBy.push_back(token.front());
    }
    return true;
}

[[nodiscard]] bool parseInterfaceMapTokens(
    const std::string& interfaceName,
    const std::string& mapText,
    Rs485InterfaceDefinition& output,
    std::string& errorMessage) {
    const auto mapTokens = split(mapText, '|');
    for (const auto& token : mapTokens) {
        const auto colonPos = token.find(':');
        if (colonPos == std::string::npos) {
            errorMessage = std::format(
                "invalid map token '{}' for interface '{}' in [rs485interface.interfaces] "
                "(expected key:value)",
                token,
                interfaceName);
            return false;
        }

        const std::string mapKey = trimCopy(token.substr(0U, colonPos));
        const std::string mapValueText = trimCopy(token.substr(colonPos + 1U));
        if (mapKey.empty()) {
            errorMessage = std::format(
                "invalid map key for interface '{}' in [rs485interface.interfaces]",
                interfaceName);
            return false;
        }

        const auto parsedValue = IniDocument::parseUnsigned(mapValueText, 0U, 65535U);
        if (!parsedValue.has_value()) {
            errorMessage = std::format(
                "invalid map value '{}' for interface '{}' in [rs485interface.interfaces] "
                "(expected unsigned 0..65535)",
                mapValueText,
                interfaceName);
            return false;
        }

        output.map[mapKey] = static_cast<std::uint16_t>(*parsedValue);
    }
    return true;
}

[[nodiscard]] bool parseInterfaceEntry(
    const IniDocument::Entry& entry,
    std::string& interfaceName,
    Rs485InterfaceDefinition& output,
    std::string& errorMessage) {
    interfaceName = trimCopy(entry.key);
    if (interfaceName.empty()) {
        errorMessage = "invalid key in [rs485interface.interfaces] (interface name must not be empty)";
        return false;
    }

    const ParsedInterfaceSegments segments = extractInterfaceSegments(entry.value);
    if (segments.usedByText.empty()) {
        errorMessage = std::format(
            "invalid interface '{}' in [rs485interface.interfaces] (missing usedby=...)",
            interfaceName);
        return false;
    }
    if (segments.mapText.empty()) {
        errorMessage = std::format(
            "invalid interface '{}' in [rs485interface.interfaces] (missing map=...)",
            interfaceName);
        return false;
    }

    if (!parseInterfaceUsedByTokens(interfaceName, segments.usedByText, output, errorMessage)) {
        return false;
    }
    if (!parseInterfaceMapTokens(interfaceName, segments.mapText, output, errorMessage)) {
        return false;
    }

    if (output.usedBy.empty() || output.map.empty()) {
        errorMessage = std::format(
            "invalid interface '{}' in [rs485interface.interfaces] (usedby/map must not be empty)",
            interfaceName);
        return false;
    }

    return true;
}

[[nodiscard]] bool parseInterfacesSection(
    const IniDocument& document,
    std::unordered_map<std::string, Rs485InterfaceDefinition>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection("rs485interface.interfaces");
    if (section == nullptr || section->entries().empty()) {
        errorMessage = "missing required section [rs485interface.interfaces]";
        return false;
    }

    std::unordered_map<std::string, Rs485InterfaceDefinition> parsed{};
    for (const auto& entry : section->entries()) {
        std::string interfaceName{};
        Rs485InterfaceDefinition parsedDefinition{};
        if (!parseInterfaceEntry(entry, interfaceName, parsedDefinition, errorMessage)) {
            return false;
        }
        parsed[interfaceName] = std::move(parsedDefinition);
    }

    output = std::move(parsed);
    return true;
}

[[nodiscard]] bool parseCommandMapSection(
    const IniDocument& document,
    const std::string_view sectionName,
    std::unordered_map<char, std::string>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection(sectionName);
    if (section == nullptr || section->entries().empty()) {
        errorMessage = std::format("missing required section [{}]", sectionName);
        return false;
    }

    std::unordered_map<char, std::string> parsed{};
    for (const auto& entry : section->entries()) {
        char command = '\0';
        if (!parseSingleCommandKey(entry.key, sectionName, command, errorMessage)) {
            return false;
        }

        const std::string topicSuffix = trimCopy(entry.value);
        if (topicSuffix.empty()) {
            errorMessage = std::format(
                "invalid value for [{}].{} (topic suffix must not be empty)",
                sectionName,
                entry.key);
            return false;
        }

        parsed[command] = topicSuffix;
    }

    output = std::move(parsed);
    return true;
}

[[nodiscard]] bool parseAddressesSection(
    const IniDocument& document,
    std::unordered_map<std::string, std::uint8_t>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection("rs485interface.addresses");
    if (section == nullptr || section->entries().empty()) {
        errorMessage = "missing required section [rs485interface.addresses]";
        return false;
    }

    std::unordered_map<std::string, std::uint8_t> parsed{};
    for (const auto& entry : section->entries()) {
        const std::string topicPrefix = trimCopy(entry.key);
        if (topicPrefix.empty()) {
            errorMessage = "invalid key in [rs485interface.addresses] (topic prefix must not be empty)";
            return false;
        }

        const auto address = IniDocument::parseUnsigned(entry.value, 1U, 127U);
        if (!address.has_value()) {
            errorMessage = std::format(
                "invalid value for [rs485interface.addresses].{} (expected unsigned 1..127)",
                topicPrefix);
            return false;
        }

        parsed[topicPrefix] = static_cast<std::uint8_t>(*address);
    }

    output = std::move(parsed);
    return true;
}

} // namespace

bool tryLoadRs485InterfaceConfigFromIni(
    const IniDocument& document,
    Rs485InterfaceConfig& output,
    std::string& errorMessage) {
    Rs485InterfaceConfig parsed{};

    if (!requireNonEmptyString(
            document,
            "rs485interface",
            "serialPortName",
            parsed.serialPortName,
            errorMessage)) {
        return false;
    }

    const auto baudrateResult = document.readUnsigned("rs485interface", "baudrate", 1U, 4000000U);
    if (!baudrateResult.second.empty()) {
        errorMessage = baudrateResult.second;
        return false;
    }
    if (baudrateResult.first.has_value()) {
        parsed.baudrate = static_cast<std::uint32_t>(*baudrateResult.first);
    }

    const auto myAddressResult = document.readUnsigned("rs485interface", "myAddress", 1U, 127U);
    if (!myAddressResult.second.empty()) {
        errorMessage = myAddressResult.second;
        return false;
    }
    if (myAddressResult.first.has_value()) {
        parsed.myAddress = static_cast<std::uint8_t>(*myAddressResult.first);
    }

    const auto maxVersionResult = document.readUnsigned("rs485interface", "maxVersion", 0U, 2U);
    if (!maxVersionResult.second.empty()) {
        errorMessage = maxVersionResult.second;
        return false;
    }
    if (maxVersionResult.first.has_value()) {
        parsed.maxVersion = static_cast<std::uint8_t>(*maxVersionResult.first);
    }

    const auto tickDelayResult = document.readUnsigned("rs485interface", "tickDelay", 1U, 600000U);
    if (!tickDelayResult.second.empty()) {
        errorMessage = tickDelayResult.second;
        return false;
    }
    if (tickDelayResult.first.has_value()) {
        parsed.tickDelayMs = static_cast<std::uint32_t>(*tickDelayResult.first);
    }

    const auto timeOfDayResult =
        document.readUnsigned("rs485interface", "timeOfDayDelayInSeconds", 1U, 86400U);
    if (!timeOfDayResult.second.empty()) {
        errorMessage = timeOfDayResult.second;
        return false;
    }
    if (timeOfDayResult.first.has_value()) {
        parsed.timeOfDayDelaySeconds = static_cast<std::uint32_t>(*timeOfDayResult.first);
    }

    const auto qosResult = document.readUnsigned("rs485interface", "qos", 0U, 2U);
    if (!qosResult.second.empty()) {
        errorMessage = qosResult.second;
        return false;
    }
    if (qosResult.first.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*qosResult.first);
    }

    if (const auto trace = document.lastValue("rs485interface", "trace"); trace.has_value()) {
        if (!parseTraceLevel(trimCopy(*trace), parsed.traceLevel, errorMessage)) {
            return false;
        }
    }

    const auto blinkDelayResult =
        document.readUnsigned("rs485interface", "blinkDelayInSeconds", 1U, 86400U);
    if (!blinkDelayResult.second.empty()) {
        errorMessage = blinkDelayResult.second;
        return false;
    }
    if (blinkDelayResult.first.has_value()) {
        parsed.blinkDelaySeconds = static_cast<std::uint32_t>(*blinkDelayResult.first);
    }

    const auto temporaryDelayResult =
        document.readUnsigned("rs485interface", "temporaryOnInSeconds", 1U, 86400U);
    if (!temporaryDelayResult.second.empty()) {
        errorMessage = temporaryDelayResult.second;
        return false;
    }
    if (temporaryDelayResult.first.has_value()) {
        parsed.temporaryOnSeconds = static_cast<std::uint32_t>(*temporaryDelayResult.first);
    }

    if (!parseInterfacesSection(document, parsed.interfaces, errorMessage)) {
        return false;
    }
    if (!parseCommandMapSection(document, "rs485interface.settings", parsed.settings, errorMessage)) {
        return false;
    }
    if (!parseCommandMapSection(document, "rs485interface.status", parsed.status, errorMessage)) {
        return false;
    }
    if (!parseAddressesSection(document, parsed.addresses, errorMessage)) {
        return false;
    }
    if (!parseTopicsSection(document, parsed.topics, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

bool tryLoadRs485InterfaceClientRuntimeConfigFromIni(
    const IniDocument& document,
    Rs485InterfaceRuntimeConfig& output,
    std::string& errorMessage) {
    Rs485InterfaceRuntimeConfig parsed{};
    if (!tryLoadRs485InterfaceConfigFromIni(document, parsed.rs485Config, errorMessage)) {
        return false;
    }

    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
