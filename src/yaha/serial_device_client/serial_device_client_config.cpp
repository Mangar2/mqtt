#include "yaha/serial_device_client/serial_device_client_config.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yaha {
namespace {

[[nodiscard]] std::string trimCopy(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, (last - first) + 1U);
}

[[nodiscard]] std::vector<std::string> split(const std::string& text, const char delimiter) {
    std::vector<std::string> tokens{};
    std::stringstream stream{text};
    std::string token{};
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(trimCopy(token));
    }
    return tokens;
}

void logConfigFallbackWarning(
    const std::string_view sectionName,
    const std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText) {
    std::cerr << "serial_device_client[warn] config_fallback"
              << " section=" << sectionName
              << " key=" << keyName
              << " value='" << rawValue << "'"
              << " default='" << defaultValue << "'"
              << " reason='" << reasonText << "'"
              << '\n' << std::flush;
}

bool requireNonEmptyString(
    const IniDocument& document,
    const std::string_view sectionName,
    const std::string_view keyName,
    std::string& output,
    std::string& errorMessage) {
    const auto value = document.lastValue(sectionName, keyName);
    if (!value.has_value() || trimCopy(*value).empty()) {
        errorMessage = std::format("missing required setting '{}.{}'", sectionName, keyName);
        return false;
    }

    output = trimCopy(*value);
    return true;
}

bool parseTraceLevel(
    const std::string& text,
    std::string& output,
    std::string& errorMessage) {
    if (text == "errors" || text == "messages" || text == "internal") {
        output = text;
        return true;
    }

    errorMessage = std::format(
        "invalid value for serialdevice.trace (expected one of: errors,messages,internal; got '{}')",
        text);
    return false;
}

void ensureDefaultInterfaces(SerialDeviceConfig& config) {
    config.interfaces.clear();

    SerialDeviceInterfaceDefinition i2c{};
    i2c.receiverMapProvided = true;
    config.interfaces["i2c"] = std::move(i2c);

    SerialDeviceInterfaceDefinition fs20{};
    fs20.receiverMapProvided = false;
    config.interfaces["fs20"] = std::move(fs20);

    SerialDeviceInterfaceDefinition switchIf{};
    switchIf.receiverMapProvided = false;
    config.interfaces["switch"] = std::move(switchIf);

    SerialDeviceInterfaceDefinition serial{};
    serial.receiverMapProvided = true;
    config.interfaces["serial"] = std::move(serial);
}

void parseCommandMapSection(
    const IniDocument& document,
    const std::string_view sectionName,
    std::unordered_map<std::string, std::string>& output,
    const bool clearOnParse,
    std::string& warningText) {
    const IniDocument::Section* section = document.findSection(sectionName);
    if (section == nullptr) {
        return;
    }

    if (clearOnParse) {
        output.clear();
    }

    for (const auto& entry : section->entries()) {
        const std::string commandKey = trimCopy(entry.key);
        const std::string topicSuffix = trimCopy(entry.value);
        if (commandKey.empty() || topicSuffix.empty()) {
            warningText = std::format(
                "invalid mapping in [{}] (key and topic suffix must not be empty)",
                sectionName);
            continue;
        }
        output[commandKey] = topicSuffix;
    }
}

void parseReceiverMapSection(
    const IniDocument& document,
    const std::string_view sectionName,
    SerialDeviceInterfaceDefinition& output,
    std::string& warningText) {
    const IniDocument::Section* section = document.findSection(sectionName);
    if (section == nullptr) {
        return;
    }

    output.receiverMapProvided = true;
    output.receiverMap.clear();

    for (const auto& entry : section->entries()) {
        const std::string topicPrefix = trimCopy(entry.key);
        const std::string addressValue = trimCopy(entry.value);
        if (topicPrefix.empty() || addressValue.empty()) {
            warningText = std::format(
                "invalid mapping in [{}] (topic prefix and address must not be empty)",
                sectionName);
            continue;
        }
        output.receiverMap[topicPrefix] = addressValue;
    }
}

void parseSwitchTopicMapSection(
    const IniDocument& document,
    SerialDeviceInterfaceDefinition& output,
    std::string& warningText) {
    const IniDocument::Section* section = document.findSection("serialdevice.switch.topicMap");
    if (section == nullptr) {
        return;
    }

    output.topicMap.clear();

    for (const auto& entry : section->entries()) {
        const std::string topic = trimCopy(entry.key);
        const auto parts = split(entry.value, ',');
        if (topic.empty() || parts.size() != 3U) {
            warningText = "invalid mapping in [serialdevice.switch.topicMap] (expected command,value,address)";
            continue;
        }

        const auto value = IniDocument::parseUnsigned(parts[1], 0U, 65535U);
        if (!value.has_value()) {
            warningText = std::format(
                "invalid value '{}' in [serialdevice.switch.topicMap]",
                parts[1]);
            continue;
        }

        output.topicMap[topic] = SerialDeviceSwitchTopicMapping{
            .command = parts[0],
            .value = static_cast<std::uint16_t>(*value),
            .address = parts[2],
        };
    }
}

void parseValueMapSection(
    const IniDocument& document,
    SerialDeviceInterfaceDefinition& output,
    std::string& warningText) {
    const IniDocument::Section* section = document.findSection("serialdevice.serial.valueMap");
    if (section == nullptr) {
        return;
    }

    output.valueMap.clear();

    const auto parseMapToken = [&](const std::string& mapToken, SerialDeviceValueMapDefinition& item) {
        const auto separator = mapToken.find(':');
        if (separator == std::string::npos) {
            warningText = std::format(
                "invalid map token '{}' in [serialdevice.serial.valueMap]",
                mapToken);
            return;
        }

        const std::string mapKey = trimCopy(mapToken.substr(0U, separator));
        const std::string mapValueText = trimCopy(mapToken.substr(separator + 1U));
        const auto mapValue = IniDocument::parseUnsigned(mapValueText, 0U, 65535U);
        if (mapKey.empty() || !mapValue.has_value()) {
            warningText = std::format(
                "invalid map token '{}' in [serialdevice.serial.valueMap]",
                mapToken);
            return;
        }

        item.map[mapKey] = static_cast<std::uint16_t>(*mapValue);
    };

    for (const auto& entry : section->entries()) {
        const std::string itemName = trimCopy(entry.key);
        if (itemName.empty()) {
            warningText = "invalid entry in [serialdevice.serial.valueMap] (empty key)";
            continue;
        }

        SerialDeviceValueMapDefinition item{};
        std::string usedByText{};
        std::string mapText{};
        const auto segments = split(entry.value, ';');
        for (const auto& segment : segments) {
            const auto separator = segment.find('=');
            if (separator == std::string::npos) {
                continue;
            }

            const std::string name = trimCopy(segment.substr(0U, separator));
            const std::string value = trimCopy(segment.substr(separator + 1U));
            if (name == "description") {
                item.description = value;
            } else if (name == "usedby") {
                usedByText = value;
            } else if (name == "map") {
                mapText = value;
            }
        }

        if (usedByText.empty() || mapText.empty()) {
            warningText = std::format(
                "invalid item '{}' in [serialdevice.serial.valueMap] (missing usedby/map)",
                itemName);
            continue;
        }

        item.usedBy = split(usedByText, ',');
        const auto mapTokens = split(mapText, '|');
        for (const auto& mapToken : mapTokens) {
            parseMapToken(mapToken, item);
        }

        output.valueMap[itemName] = std::move(item);
    }
}

} // namespace

bool tryLoadSerialDeviceConfigFromIni(
    const IniDocument& document,
    SerialDeviceConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    SerialDeviceConfig parsed{};
    ensureDefaultInterfaces(parsed);

    if (!requireNonEmptyString(document, "serialdevice", "serialPortName", parsed.serialPortName, errorMessage)) {
        return false;
    }

    const auto baudrateResult = document.readUnsigned("serialdevice", "baudrate", 1U, 4000000U);
    if (!baudrateResult.second.empty()) {
        const std::string rawValue = document.lastValue("serialdevice", "baudrate").value_or("<missing>");
        logConfigFallbackWarning(
            "serialdevice",
            "baudrate",
            rawValue,
            std::to_string(parsed.baudrate),
            baudrateResult.second);
    }
    if (baudrateResult.first.has_value()) {
        parsed.baudrate = static_cast<std::uint32_t>(*baudrateResult.first);
    }

    const auto qosResult = document.readUnsigned("serialdevice", "qos", 0U, 2U);
    if (!qosResult.second.empty()) {
        const std::string rawValue = document.lastValue("serialdevice", "qos").value_or("<missing>");
        logConfigFallbackWarning(
            "serialdevice",
            "qos",
            rawValue,
            std::to_string(static_cast<unsigned int>(parsed.subscribeQos)),
            qosResult.second);
    }
    if (qosResult.first.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*qosResult.first);
    }

    const auto keepAliveResult = document.readUnsigned("serialdevice", "keepAliveDelayInSeconds", 1U, 86400U);
    if (!keepAliveResult.second.empty()) {
        const std::string rawValue = document.lastValue("serialdevice", "keepAliveDelayInSeconds").value_or("<missing>");
        logConfigFallbackWarning(
            "serialdevice",
            "keepAliveDelayInSeconds",
            rawValue,
            std::to_string(parsed.keepAliveDelayInSeconds),
            keepAliveResult.second);
    }
    if (keepAliveResult.first.has_value()) {
        parsed.keepAliveDelayInSeconds = static_cast<std::uint32_t>(*keepAliveResult.first);
    }

    if (const auto trace = document.lastValue("serialdevice", "trace"); trace.has_value()) {
        std::string warningText{};
        const std::string traceValue = trimCopy(*trace);
        if (!parseTraceLevel(traceValue, parsed.traceLevel, warningText)) {
            logConfigFallbackWarning("serialdevice", "trace", traceValue, parsed.traceLevel, warningText);
        }
    }

    std::string warningText{};
    parseCommandMapSection(
        document,
        "serialdevice.i2c.commandMap",
        parsed.interfaces["i2c"].commandMap,
        true,
        warningText);
    parseReceiverMapSection(document, "serialdevice.i2c.receiverMap", parsed.interfaces["i2c"], warningText);

    parseCommandMapSection(
        document,
        "serialdevice.fs20.commandMap",
        parsed.interfaces["fs20"].commandMap,
        true,
        warningText);
    parseCommandMapSection(
        document,
        "serialdevice.fs20.sendMap",
        parsed.interfaces["fs20"].sendMap,
        true,
        warningText);

    parseSwitchTopicMapSection(document, parsed.interfaces["switch"], warningText);

    parseCommandMapSection(
        document,
        "serialdevice.serial.commandMap",
        parsed.interfaces["serial"].commandMap,
        true,
        warningText);
    parseReceiverMapSection(document, "serialdevice.serial.receiverMap", parsed.interfaces["serial"], warningText);
    parseValueMapSection(document, parsed.interfaces["serial"], warningText);

    if (!warningText.empty()) {
        logConfigFallbackWarning("serialdevice", "interfaces", "<composite>", "defaults", warningText);
    }

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

bool tryLoadSerialDeviceClientRuntimeConfigFromIni(
    const IniDocument& document,
    SerialDeviceClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    SerialDeviceClientRuntimeConfig parsed{};
    if (!tryLoadSerialDeviceConfigFromIni(document, parsed.serialDeviceConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        logConfigFallbackWarning(
            "mqtt",
            "*",
            "<composite>",
            "defaults",
            mqttErrorMessage);
    }

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha
