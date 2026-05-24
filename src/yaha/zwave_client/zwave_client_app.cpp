#include "yaha/zwave_client/zwave_client_app.h"

#include "httplib.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
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
constexpr std::uint64_t kLogLevelMin = 0U;
constexpr std::uint64_t kLogLevelMax = 4U;
constexpr std::uint64_t kPollIntervalMsMin = 1U;
constexpr std::uint64_t kPollIntervalMsMax =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr std::uint64_t kCommandReactionPollIntervalMsMin = 1U;
constexpr std::uint64_t kCommandReactionPollIntervalMsMax =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr std::uint64_t kCommandReactionTimeoutMsMin = 1U;
constexpr std::uint64_t kCommandReactionTimeoutMsMax =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr int kFileStoreConnectTimeoutSeconds = 1;
constexpr int kFileStoreReadTimeoutSeconds = 1;
constexpr int kFileStoreWriteTimeoutSeconds = 1;
constexpr int kHttpOkStatus = 200;
constexpr int kHttpNotFoundStatus = 404;

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

[[nodiscard]] bool parseZwaveLoggingSettings(
    const IniDocument& document,
    ZwaveConfig& parsed,
    std::string& errorMessage) {
    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.logIncomingMessages,
        .enableOutgoing = parsed.logOutgoingMessages,
        .includeReasonChain = true,
    };
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = "zwave", .key = "logIncomingMessages"},
                .outgoingEnabled = MessageLogIniBoolKey{.section = "zwave", .key = "logOutgoingMessages"},
                .includeReasonChain = std::nullopt,
            },
            messageLogConfig,
            errorMessage)) {
        return false;
    }

    parsed.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.logOutgoingMessages = messageLogConfig.enableOutgoing;

    const auto logLevelResult = document.readUnsigned("zwave", "logLevel", kLogLevelMin, kLogLevelMax);
    if (!logLevelResult.second.empty()) {
        errorMessage = logLevelResult.second;
        return false;
    }
    if (logLevelResult.first.has_value()) {
        parsed.logLevel = static_cast<std::uint8_t>(*logLevelResult.first);
    }

    return true;
}

[[nodiscard]] bool parseZwaveTimingSettings(
    const IniDocument& document,
    ZwaveConfig& parsed,
    std::string& errorMessage) {
    const auto pollIntervalResult =
        document.readUnsigned("zwave", "pollIntervalMs", kPollIntervalMsMin, kPollIntervalMsMax);
    if (!pollIntervalResult.second.empty()) {
        errorMessage = pollIntervalResult.second;
        return false;
    }
    if (pollIntervalResult.first.has_value()) {
        parsed.pollIntervalMs = static_cast<std::int64_t>(*pollIntervalResult.first);
    }

    const auto commandReactionPollIntervalResult = document.readUnsigned(
        "zwave",
        "commandReactionPollIntervalMs",
        kCommandReactionPollIntervalMsMin,
        kCommandReactionPollIntervalMsMax);
    if (!commandReactionPollIntervalResult.second.empty()) {
        errorMessage = commandReactionPollIntervalResult.second;
        return false;
    }
    if (commandReactionPollIntervalResult.first.has_value()) {
        parsed.commandReactionPollIntervalMs = static_cast<std::int64_t>(*commandReactionPollIntervalResult.first);
    }

    const auto commandReactionTimeoutResult = document.readUnsigned(
        "zwave",
        "commandReactionTimeoutMs",
        kCommandReactionTimeoutMsMin,
        kCommandReactionTimeoutMsMax);
    if (!commandReactionTimeoutResult.second.empty()) {
        errorMessage = commandReactionTimeoutResult.second;
        return false;
    }
    if (commandReactionTimeoutResult.first.has_value()) {
        parsed.commandReactionTimeoutMs = static_cast<std::int64_t>(*commandReactionTimeoutResult.first);
    }

    return true;
}

[[nodiscard]] bool parseFileStoreSettings(
    const IniDocument& document,
    ZwaveConfig& parsed,
    std::string& errorMessage) {
    if (const auto fileStoreHost = document.lastValue("filestore", "host"); fileStoreHost.has_value()) {
        parsed.fileStoreHost = *fileStoreHost;
    }

    const auto fileStorePortResult = document.readUnsigned("filestore", "port", 1U, 65535U);
    if (!fileStorePortResult.second.empty()) {
        errorMessage = fileStorePortResult.second;
        return false;
    }
    if (fileStorePortResult.first.has_value()) {
        parsed.fileStorePort = static_cast<std::uint16_t>(*fileStorePortResult.first);
    }

    const auto fileStoreUseResult = document.readBool("filestore", "use");
    if (!fileStoreUseResult.second.empty()) {
        errorMessage = fileStoreUseResult.second;
        return false;
    }
    if (fileStoreUseResult.first.has_value()) {
        parsed.fileStoreEnabled = *fileStoreUseResult.first;
    }

    if (const auto settingsKeyPath = document.lastValue("filestore", "filename"); settingsKeyPath.has_value()) {
        parsed.settingsKeyPath = *settingsKeyPath;
    }

    if (const auto monitorTopicPrefix = document.lastValue("filestore", "topicPrefix");
        monitorTopicPrefix.has_value()) {
        parsed.fileStoreMonitorTopicPrefix = *monitorTopicPrefix;
    }

    const auto retryCountResult = document.readUnsigned("filestore", "startupRetryCount", 0U, 1000U);
    if (!retryCountResult.second.empty()) {
        errorMessage = retryCountResult.second;
        return false;
    }
    if (retryCountResult.first.has_value()) {
        parsed.fileStoreStartupRetryCount = static_cast<std::uint32_t>(*retryCountResult.first);
    }

    const auto retryIntervalResult = document.readUnsigned("filestore", "startupRetryIntervalSeconds", 1U, 3600U);
    if (!retryIntervalResult.second.empty()) {
        errorMessage = retryIntervalResult.second;
        return false;
    }
    if (retryIntervalResult.first.has_value()) {
        parsed.fileStoreStartupRetryIntervalSeconds = static_cast<std::uint32_t>(*retryIntervalResult.first);
    }

    return true;
}

void configureFileStoreClientTimeouts(httplib::Client* client) {
    if (client == nullptr) {
        return;
    }

    client->set_connection_timeout(kFileStoreConnectTimeoutSeconds, 0);
    client->set_read_timeout(kFileStoreReadTimeoutSeconds, 0);
    client->set_write_timeout(kFileStoreWriteTimeoutSeconds, 0);
}

void skipWhitespace(const std::string& text, std::size_t& parseIndex) {
    while (parseIndex < text.size() && std::isspace(static_cast<unsigned char>(text[parseIndex])) != 0) {
        parseIndex += 1U;
    }
}

[[nodiscard]] bool consumeChar(const std::string& text, std::size_t& parseIndex, const char expectedChar) {
    skipWhitespace(text, parseIndex);
    if (parseIndex >= text.size() || text[parseIndex] != expectedChar) {
        return false;
    }
    parseIndex += 1U;
    return true;
}

[[nodiscard]] bool parseJsonStringToken(
    const std::string& jsonText,
    std::size_t& parseIndex,
    std::string& output) {
    if (parseIndex >= jsonText.size() || jsonText[parseIndex] != '"') {
        return false;
    }
    parseIndex += 1U;

    std::string valueText{};
    while (parseIndex < jsonText.size()) {
        const char currentChar = jsonText[parseIndex++];
        if (currentChar == '"') {
            output = std::move(valueText);
            return true;
        }

        if (currentChar == '\\') {
            if (parseIndex >= jsonText.size()) {
                return false;
            }

            const char escapedChar = jsonText[parseIndex++];
            switch (escapedChar) {
            case '"':
            case '\\':
            case '/':
                valueText.push_back(escapedChar);
                break;
            case 'n':
                valueText.push_back('\n');
                break;
            case 'r':
                valueText.push_back('\r');
                break;
            case 't':
                valueText.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        valueText.push_back(currentChar);
    }

    return false;
}

[[nodiscard]] bool parseJsonUnsignedToken(const std::string& text, std::size_t& parseIndex, std::uint64_t& output) {
    skipWhitespace(text, parseIndex);
    if (parseIndex >= text.size() || std::isdigit(static_cast<unsigned char>(text[parseIndex])) == 0) {
        return false;
    }

    std::size_t tokenEnd = parseIndex;
    while (tokenEnd < text.size() && std::isdigit(static_cast<unsigned char>(text[tokenEnd])) != 0) {
        tokenEnd += 1U;
    }

    const std::string numberText = text.substr(parseIndex, tokenEnd - parseIndex);
    const auto parsedValue = IniDocument::parseUnsigned(numberText, 0U, std::numeric_limits<std::uint64_t>::max());
    if (!parsedValue.has_value()) {
        return false;
    }

    output = *parsedValue;
    parseIndex = tokenEnd;
    return true;
}

struct DeviceJsonDraft {
    std::optional<std::string> topic{};
    std::optional<std::uint64_t> nodeId{};
    std::optional<std::uint64_t> classId{};
    std::optional<std::uint64_t> instance{};
    std::optional<std::uint64_t> index{};
    std::optional<std::string> type{};
    std::optional<std::string> label{};
};

[[nodiscard]] bool parseJsonDeviceField(
    const std::string& key,
    const std::string& text,
    std::size_t& parseIndex,
    DeviceJsonDraft& output,
    std::string& errorMessage) {
    auto parseStringValue = [&](const char* errorPrefix, std::optional<std::string>& target) {
        std::string value{};
        if (!parseJsonStringToken(text, parseIndex, value)) {
            errorMessage = std::string{"invalid settings json: "} + errorPrefix;
            return false;
        }
        target = std::move(value);
        return true;
    };

    auto parseNumberValue = [&](const char* errorPrefix, std::optional<std::uint64_t>& target) {
        std::uint64_t value = 0U;
        if (!parseJsonUnsignedToken(text, parseIndex, value)) {
            errorMessage = std::string{"invalid settings json: "} + errorPrefix;
            return false;
        }
        target = value;
        return true;
    };

    if (key == "topic") {
        return parseStringValue("device.topic must be string", output.topic);
    }
    if (key == "nodeId") {
        return parseNumberValue("device.nodeId must be number", output.nodeId);
    }
    if (key == "classId") {
        return parseNumberValue("device.classId must be number", output.classId);
    }
    if (key == "instance") {
        return parseNumberValue("device.instance must be number", output.instance);
    }
    if (key == "index") {
        return parseNumberValue("device.index must be number", output.index);
    }
    if (key == "type") {
        return parseStringValue("device.type must be string", output.type);
    }
    if (key == "label") {
        return parseStringValue("device.label must be string", output.label);
    }

    errorMessage = "invalid settings json: unknown device field '" + key + "'";
    return false;
}

[[nodiscard]] bool buildDeviceConfigFromDraft(
    const DeviceJsonDraft& draft,
    ZwaveDeviceConfig& output,
    std::string& errorMessage) {
    if (!draft.topic.has_value() || draft.topic->empty()) {
        errorMessage = "invalid settings json: device.topic missing or empty";
        return false;
    }
    if (!draft.nodeId.has_value() || *draft.nodeId < kNodeIdMin || *draft.nodeId > kNodeIdMax) {
        errorMessage = "invalid settings json: device.nodeId out of range";
        return false;
    }
    if (draft.classId.has_value() && (*draft.classId < kClassIdMin || *draft.classId > kClassIdMax)) {
        errorMessage = "invalid settings json: device.classId out of range";
        return false;
    }
    if (draft.instance.has_value() && (*draft.instance < kInstanceMin || *draft.instance > kInstanceMax)) {
        errorMessage = "invalid settings json: device.instance out of range";
        return false;
    }
    if (draft.index.has_value() && (*draft.index < kIndexMin || *draft.index > kIndexMax)) {
        errorMessage = "invalid settings json: device.index out of range";
        return false;
    }

    ZwaveDeviceConfig parsed{};
    parsed.topic = *draft.topic;
    parsed.nodeId = static_cast<std::uint16_t>(*draft.nodeId);
    if (draft.classId.has_value()) {
        parsed.classId = static_cast<std::uint16_t>(*draft.classId);
    }
    if (draft.instance.has_value()) {
        parsed.instance = static_cast<std::uint8_t>(*draft.instance);
    }
    if (draft.index.has_value()) {
        parsed.index = static_cast<std::uint8_t>(*draft.index);
    }
    if (draft.type.has_value() && !draft.type->empty()) {
        parsed.type = *draft.type;
    }
    if (draft.label.has_value() && !draft.label->empty()) {
        parsed.label = *draft.label;
    }

    output = std::move(parsed);
    return true;
}

[[nodiscard]] bool parseJsonDeviceObject(
    const std::string& text,
    std::size_t& parseIndex,
    ZwaveDeviceConfig& output,
    std::string& errorMessage) {
    if (!consumeChar(text, parseIndex, '{')) {
        errorMessage = "invalid settings json: expected device object";
        return false;
    }

    DeviceJsonDraft draft{};
    while (true) {
        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        std::string key{};
        if (!parseJsonStringToken(text, parseIndex, key)) {
            errorMessage = "invalid settings json: expected device key";
            return false;
        }
        if (!consumeChar(text, parseIndex, ':')) {
            errorMessage = "invalid settings json: expected ':' after device key";
            return false;
        }
        if (!parseJsonDeviceField(key, text, parseIndex, draft, errorMessage)) {
            return false;
        }

        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }
        if (parseIndex < text.size() && text[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }
        errorMessage = "invalid settings json: malformed device object";
        return false;
    }

    return buildDeviceConfigFromDraft(draft, output, errorMessage);
}

[[nodiscard]] bool parseJsonDevicesArray(
    const std::string& text,
    std::size_t& parseIndex,
    std::vector<ZwaveDeviceConfig>& devices,
    std::string& errorMessage) {
    if (!consumeChar(text, parseIndex, '[')) {
        errorMessage = "invalid settings json: expected devices array";
        return false;
    }

    while (true) {
        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }

        ZwaveDeviceConfig device{};
        if (!parseJsonDeviceObject(text, parseIndex, device, errorMessage)) {
            return false;
        }
        devices.push_back(std::move(device));

        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }
        if (parseIndex < text.size() && text[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }
        errorMessage = "invalid settings json: malformed devices array";
        return false;
    }
}

[[nodiscard]] bool parseJsonRootEntry(
    const std::string& text,
    std::size_t& parseIndex,
    std::vector<ZwaveDeviceConfig>& devices,
    bool& hasDevices,
    std::string& errorMessage) {
    std::string key{};
    if (!parseJsonStringToken(text, parseIndex, key)) {
        errorMessage = "invalid settings json: expected root key";
        return false;
    }
    if (!consumeChar(text, parseIndex, ':')) {
        errorMessage = "invalid settings json: expected ':' after root key";
        return false;
    }

    if (key != "devices") {
        errorMessage = "invalid settings json: unknown root key '" + key + "'";
        return false;
    }

    hasDevices = true;
    return parseJsonDevicesArray(text, parseIndex, devices, errorMessage);
}

[[nodiscard]] bool parseJsonRootEntries(
    const std::string& text,
    std::size_t& parseIndex,
    std::vector<ZwaveDeviceConfig>& devices,
    bool& hasDevices,
    std::string& errorMessage) {
    while (true) {
        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == '}') {
            parseIndex += 1U;
            return true;
        }

        if (!parseJsonRootEntry(text, parseIndex, devices, hasDevices, errorMessage)) {
            return false;
        }

        skipWhitespace(text, parseIndex);
        if (parseIndex < text.size() && text[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }
        if (parseIndex < text.size() && text[parseIndex] == '}') {
            parseIndex += 1U;
            return true;
        }

        errorMessage = "invalid settings json: malformed root object";
        return false;
    }
}

[[nodiscard]] bool parseJsonRootDevices(
    const std::string& text,
    std::vector<ZwaveDeviceConfig>& devices,
    std::string& errorMessage) {
    devices.clear();
    std::size_t parseIndex = 0U;
    bool hasDevices = false;
    if (!consumeChar(text, parseIndex, '{')) {
        errorMessage = "invalid settings json: expected root object";
        return false;
    }

    if (!parseJsonRootEntries(text, parseIndex, devices, hasDevices, errorMessage)) {
        return false;
    }

    skipWhitespace(text, parseIndex);
    if (parseIndex != text.size()) {
        errorMessage = "invalid settings json: trailing characters";
        return false;
    }
    if (!hasDevices) {
        errorMessage = "invalid settings json: missing devices array";
        return false;
    }
    return true;
}

void applyNodeOverrideDevices(
    const std::vector<ZwaveDeviceConfig>& fileStoreDevices,
    ZwaveConfig& output) {
    std::set<std::uint16_t> fileStoreNodeIds{};
    for (const auto& device : fileStoreDevices) {
        fileStoreNodeIds.insert(device.nodeId);
    }

    std::vector<ZwaveDeviceConfig> mergedDevices{};
    mergedDevices.reserve(output.devices.size() + fileStoreDevices.size());

    for (const auto& iniDevice : output.devices) {
        if (!fileStoreNodeIds.contains(iniDevice.nodeId)) {
            mergedDevices.push_back(iniDevice);
        }
    }

    for (const auto& fileStoreDevice : fileStoreDevices) {
        mergedDevices.push_back(fileStoreDevice);
    }

    output.devices = std::move(mergedDevices);
}

[[nodiscard]] bool tryLoadDeviceSnapshotFromFileStore(const ZwaveConfig& config,
                                                      std::vector<ZwaveDeviceConfig>& outputDevices,
                                                      std::string& errorMessage) {
    httplib::Client client{config.fileStoreHost, static_cast<int>(config.fileStorePort)};
    configureFileStoreClientTimeouts(&client);
    const auto response = client.Get(config.settingsKeyPath);
    if (!response) {
        errorMessage = "failed to load zwave settings from filestore: no_response";
        return false;
    }

    if (response->status == kHttpNotFoundStatus) {
        outputDevices.clear();
        return true;
    }

    if (response->status != kHttpOkStatus) {
        errorMessage = "failed to load zwave settings from filestore: status=" + std::to_string(response->status);
        return false;
    }

    std::vector<ZwaveDeviceConfig> fileStoreDevices{};
    if (!parseJsonRootDevices(response->body, fileStoreDevices, errorMessage)) {
        std::cout << "zwave_client[error] op=filestore_get_settings"
                  << " path=" << config.settingsKeyPath
                  << " status=" << response->status
                  << " reason=invalid_json"
                  << " detail=\"" << errorMessage << "\""
                  << '\n' << std::flush;
        return false;
    }

    outputDevices = std::move(fileStoreDevices);
    return true;
}

void persistSettingsToFileStore(const ZwaveConfig& config) {
    httplib::Client client{config.fileStoreHost, static_cast<int>(config.fileStorePort)};
    configureFileStoreClientTimeouts(&client);
    const std::string payload = serializeZwaveSettingsToJson(config);
    const auto response = client.Post(config.settingsKeyPath, payload, "application/json");

    if (!response || response->status != kHttpOkStatus) {
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cout << "zwave_client[error] op=filestore_post_settings"
                  << " path=" << config.settingsKeyPath
                  << " status=" << statusText
                  << " reason=persist_settings_failed"
                  << '\n' << std::flush;
    }
}

void appendStringField(std::string& target, const std::string& key, const std::string& value, const bool withComma) {
    target.append("\"");
    target.append(escapeJsonString(key));
    target.append("\":\"");
    target.append(escapeJsonString(value));
    target.push_back('"');
    if (withComma) {
        target.push_back(',');
    }
}

void appendNumberField(std::string& target, const std::string& key, const std::uint64_t value, const bool withComma) {
    target.append("\"");
    target.append(escapeJsonString(key));
    target.append("\":");
    target.append(std::to_string(value));
    if (withComma) {
        target.push_back(',');
    }
}

void appendOptionalNumberField(
    std::string& target,
    const std::string& key,
    const std::optional<std::uint64_t>& value,
    bool& firstField) {
    if (!value.has_value()) {
        return;
    }
    if (!firstField) {
        target.push_back(',');
    }
    firstField = false;
    appendNumberField(target, key, *value, false);
}

void appendOptionalStringField(
    std::string& target,
    const std::string& key,
    const std::optional<std::string>& value,
    bool& firstField) {
    if (!value.has_value() || value->empty()) {
        return;
    }
    if (!firstField) {
        target.push_back(',');
    }
    firstField = false;
    appendStringField(target, key, *value, false);
}

void appendDeviceAsJson(std::string& target, const ZwaveDeviceConfig& device) {
    target.push_back('{');
    bool firstField = true;

    appendStringField(target, "topic", device.topic, false);
    firstField = false;

    target.push_back(',');
    appendNumberField(target, "nodeId", device.nodeId, false);

    appendOptionalNumberField(
        target,
        "classId",
        device.classId.has_value()
            ? std::optional<std::uint64_t>{*device.classId}
            : std::nullopt,
        firstField);
    appendOptionalNumberField(
        target,
        "instance",
        device.instance.has_value()
            ? std::optional<std::uint64_t>{*device.instance}
            : std::nullopt,
        firstField);
    appendOptionalNumberField(
        target,
        "index",
        device.index.has_value()
            ? std::optional<std::uint64_t>{*device.index}
            : std::nullopt,
        firstField);
    appendOptionalStringField(target, "type", device.type, firstField);
    appendOptionalStringField(target, "label", device.label, firstField);

    target.push_back('}');
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

    if (!parseZwaveLoggingSettings(document, parsed, errorMessage)) {
        return false;
    }

    if (!parseZwaveTimingSettings(document, parsed, errorMessage)) {
        return false;
    }

    if (!parseFileStoreSettings(document, parsed, errorMessage)) {
        return false;
    }

    if (!requireSetting(document, "zwave", "usbDevice", parsed.usb.device, errorMessage)) {
        return false;
    }

    if (!requireSetting(document, "zwave", "usbTopic", parsed.usb.topic, errorMessage)) {
        return false;
    }

    const IniDocument::Section* zwaveSection = document.findSection("zwave");
    if (zwaveSection == nullptr) {
        output = std::move(parsed);
        return true;
    }

    const auto deviceRows = zwaveSection->valuesForKey("device");
    if (!deviceRows.has_value() || deviceRows->empty()) {
        output = std::move(parsed);
        return true;
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

bool tryApplyZwaveDeviceSettingsFromJson(
    const std::string& jsonText,
    ZwaveConfig& output,
    std::string& errorMessage) {
    std::vector<ZwaveDeviceConfig> fileStoreDevices{};
    if (!parseJsonRootDevices(jsonText, fileStoreDevices, errorMessage)) {
        return false;
    }

    applyNodeOverrideDevices(fileStoreDevices, output);
    return true;
}

bool trySyncZwaveDeviceSettingsFromFileStore(
    ZwaveConfig& config,
    std::string& errorMessage) {
    if (!config.fileStoreEnabled) {
        return true;
    }

    std::vector<ZwaveDeviceConfig> loadedDevices{};
    if (!tryLoadDeviceSnapshotFromFileStore(config, loadedDevices, errorMessage)) {
        return false;
    }

    config.devices = std::move(loadedDevices);

    persistSettingsToFileStore(config);
    return true;
}

bool tryLoadZwaveDeviceSettingsSnapshotFromFileStore(
    const ZwaveConfig& config,
    std::vector<ZwaveDeviceConfig>& outputDevices,
    std::string& errorMessage) {
    if (!config.fileStoreEnabled) {
        outputDevices = config.devices;
        return true;
    }

    return tryLoadDeviceSnapshotFromFileStore(config, outputDevices, errorMessage);
}

std::string serializeZwaveSettingsToJson(const ZwaveConfig& config) {
    std::string json{"{\"devices\":["};

    for (std::size_t index = 0U; index < config.devices.size(); ++index) {
        appendDeviceAsJson(json, config.devices[index]);
        if (index + 1U < config.devices.size()) {
            json.push_back(',');
        }
    }

    json.append("]}");
    return json;
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
