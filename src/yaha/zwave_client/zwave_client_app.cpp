#include "yaha/zwave_client/zwave_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yaha {

namespace {

[[nodiscard]] std::string trimCopy(std::string input) {
    auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };

    input.erase(input.begin(), std::ranges::find_if(input, notSpace));
    input.erase(std::ranges::find_if(input.rbegin(), input.rend(), notSpace).base(), input.end());
    return input;
}
constexpr std::size_t kDeviceFieldCountMin = 2U;
constexpr std::size_t kDeviceFieldCountMax = 7U;
constexpr std::size_t kDeviceFieldTopic = 0U;
constexpr std::size_t kDeviceFieldNodeId = 1U;
constexpr std::size_t kDeviceFieldClassId = 2U;
constexpr std::size_t kDeviceFieldInstance = 3U;
constexpr std::size_t kDeviceFieldIndex = 4U;
constexpr std::size_t kDeviceFieldType = 5U;
constexpr std::size_t kDeviceFieldLabel = 6U;

constexpr std::uint64_t kNodeIdMin = 1U;
constexpr std::uint64_t kNodeIdMax = 255U;
constexpr std::uint64_t kClassIdMin = 0U;
constexpr std::uint64_t kClassIdMax = 65535U;
constexpr std::uint64_t kInstanceMin = 0U;
constexpr std::uint64_t kInstanceMax = 255U;
constexpr std::uint64_t kIndexMin = 0U;
constexpr std::uint64_t kIndexMax = 255U;

[[nodiscard]] std::vector<std::string> splitDeviceLine(const std::string& line) {
    std::vector<std::string> fields{};
    std::size_t fieldStart = 0U;

    while (fieldStart <= line.size()) {
        const std::size_t delimiterPos = line.find('|', fieldStart);
        if (delimiterPos == std::string::npos) {
            fields.push_back(trimCopy(line.substr(fieldStart)));
            break;
        }

        fields.push_back(trimCopy(line.substr(fieldStart, delimiterPos - fieldStart)));
        fieldStart = delimiterPos + 1U;
    }

    return fields;
}

[[nodiscard]] bool parseOptionalUnsignedField(
    const std::string& text,
    const std::string_view fieldName,
    const std::uint64_t minValue,
    const std::uint64_t maxValue,
    std::optional<std::uint64_t>& output,
    std::string& errorMessage) {
    if (text.empty()) {
        output = std::nullopt;
        return true;
    }

    const auto parsed = IniDocument::parseUnsigned(text, minValue, maxValue);
    if (!parsed.has_value()) {
        errorMessage = "invalid device field '" + std::string{fieldName} + "' (expected " +
            std::to_string(minValue) + ".." + std::to_string(maxValue) + ", got '" + text + "')";
        return false;
    }

    output = *parsed;
    return true;
}

[[nodiscard]] bool parseDeviceEntry(
    const std::string& line,
    ZwaveDeviceConfig& output,
    std::string& errorMessage) {
    const std::vector<std::string> fields = splitDeviceLine(line);
    if (fields.size() < kDeviceFieldCountMin || fields.size() > kDeviceFieldCountMax) {
        errorMessage = "invalid zwave.device entry (expected 'topic|nodeId|classId|instance|index|type|label')";
        return false;
    }

    if (fields[kDeviceFieldTopic].empty()) {
        errorMessage = "invalid zwave.device entry: topic must not be empty";
        return false;
    }

    const auto nodeId = IniDocument::parseUnsigned(fields[kDeviceFieldNodeId], kNodeIdMin, kNodeIdMax);
    if (!nodeId.has_value()) {
        errorMessage = "invalid zwave.device entry: nodeId must be in range " +
            std::to_string(kNodeIdMin) + ".." + std::to_string(kNodeIdMax);
        return false;
    }

    ZwaveDeviceConfig parsed{};
    parsed.topic = fields[kDeviceFieldTopic];
    parsed.nodeId = static_cast<std::uint16_t>(*nodeId);

    std::optional<std::uint64_t> parsedClassId{};
    std::optional<std::uint64_t> parsedInstance{};
    std::optional<std::uint64_t> parsedIndex{};

    if (fields.size() > kDeviceFieldClassId) {
        if (!parseOptionalUnsignedField(
                fields[kDeviceFieldClassId],
                "classId",
                kClassIdMin,
                kClassIdMax,
                parsedClassId,
                errorMessage)) {
            return false;
        }
        if (parsedClassId.has_value()) {
            parsed.classId = static_cast<std::uint16_t>(*parsedClassId);
        }
    }

    if (fields.size() > kDeviceFieldInstance) {
        if (!parseOptionalUnsignedField(
                fields[kDeviceFieldInstance],
                "instance",
                kInstanceMin,
                kInstanceMax,
                parsedInstance,
                errorMessage)) {
            return false;
        }
        if (parsedInstance.has_value()) {
            parsed.instance = static_cast<std::uint8_t>(*parsedInstance);
        }
    }

    if (fields.size() > kDeviceFieldIndex) {
        if (!parseOptionalUnsignedField(
                fields[kDeviceFieldIndex],
                "index",
                kIndexMin,
                kIndexMax,
                parsedIndex,
                errorMessage)) {
            return false;
        }
        if (parsedIndex.has_value()) {
            parsed.index = static_cast<std::uint8_t>(*parsedIndex);
        }
    }

    if (fields.size() > kDeviceFieldType && !fields[kDeviceFieldType].empty()) {
        parsed.type = fields[kDeviceFieldType];
    }

    if (fields.size() > kDeviceFieldLabel && !fields[kDeviceFieldLabel].empty()) {
        parsed.label = fields[kDeviceFieldLabel];
    }

    output = std::move(parsed);
    return true;
}

[[nodiscard]] bool requireSetting(
    const IniDocument& document,
    const std::string_view sectionName,
    const std::string_view keyName,
    std::string& output,
    std::string& errorMessage) {
    const auto value = document.lastValue(sectionName, keyName);
    if (!value.has_value() || value->empty()) {
        errorMessage = "missing required setting '" + std::string{sectionName} + "." + std::string{keyName} + "'";
        return false;
    }

    output = *value;
    return true;
}

} // namespace

bool tryLoadZwaveConfigFromIni(
    const IniDocument& document,
    ZwaveConfig& output,
    std::string& errorMessage) {
    ZwaveConfig parsed{};

    const auto subscribeQosResult = document.readUnsigned("zwave", "subscribeQoS", 0U, 2U);
    if (!subscribeQosResult.second.empty()) {
        errorMessage = subscribeQosResult.second;
        return false;
    }
    if (subscribeQosResult.first.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*subscribeQosResult.first);
    }

    const auto publishQosResult = document.readUnsigned("zwave", "qos", 0U, 2U);
    if (!publishQosResult.second.empty()) {
        errorMessage = publishQosResult.second;
        return false;
    }
    if (publishQosResult.first.has_value()) {
        parsed.qos = static_cast<Qos>(*publishQosResult.first);
    }

    const auto retainResult = document.readBool("zwave", "retain");
    if (!retainResult.second.empty()) {
        errorMessage = retainResult.second;
        return false;
    }
    if (retainResult.first.has_value()) {
        parsed.retain = *retainResult.first;
    }

    const auto logIncomingResult = document.readBool("zwave", "logIncomingMessages");
    if (!logIncomingResult.second.empty()) {
        errorMessage = logIncomingResult.second;
        return false;
    }
    if (logIncomingResult.first.has_value()) {
        parsed.logIncomingMessages = *logIncomingResult.first;
    }

    const auto logOutgoingResult = document.readBool("zwave", "logOutgoingMessages");
    if (!logOutgoingResult.second.empty()) {
        errorMessage = logOutgoingResult.second;
        return false;
    }
    if (logOutgoingResult.first.has_value()) {
        parsed.logOutgoingMessages = *logOutgoingResult.first;
    }

    if (!requireSetting(document, "zwave", "usbDevice", parsed.usb.device, errorMessage)) {
        return false;
    }

    if (!requireSetting(document, "zwave", "usbTopic", parsed.usb.topic, errorMessage)) {
        return false;
    }

    const IniDocument::Section* zwaveSection = document.findSection("zwave");
    if (zwaveSection == nullptr) {
        errorMessage = "missing required setting 'zwave.device'";
        return false;
    }

    const auto deviceRows = zwaveSection->valuesForKey("device");
    if (!deviceRows.has_value() || deviceRows->empty()) {
        errorMessage = "missing required setting 'zwave.device'";
        return false;
    }

    std::vector<ZwaveDeviceConfig> parsedDevices{};
    parsedDevices.reserve(deviceRows->size());
    for (const auto& deviceRow : *deviceRows) {
        ZwaveDeviceConfig parsedDevice{};
        if (!parseDeviceEntry(deviceRow, parsedDevice, errorMessage)) {
            return false;
        }
        parsedDevices.push_back(std::move(parsedDevice));
    }

    parsed.devices = std::move(parsedDevices);
    output = std::move(parsed);
    return true;
}

bool tryLoadZwaveClientRuntimeConfigFromIni(
    const IniDocument& document,
    ZwaveClientRuntimeConfig& output,
    std::string& errorMessage) {
    ZwaveClientRuntimeConfig parsed{};
    if (!tryLoadZwaveConfigFromIni(document, parsed.zwaveConfig, errorMessage)) {
        return false;
    }

    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
