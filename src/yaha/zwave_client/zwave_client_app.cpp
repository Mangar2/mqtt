#include "yaha/zwave_client/zwave_client_app.h"

#include "helper/string_helper.h"
#include "httplib.h"
#include "json/json_error.h"
#include "json/json_value.h"
#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <cmath>
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
            fields.push_back(mqtt::helper::trim(line.substr(fieldStart)));
            break;
        }

        fields.push_back(mqtt::helper::trim(line.substr(fieldStart, delimiterPos - fieldStart)));
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
        document.reportFallback("zwave", "log*", "<composite>", "defaults", errorMessage);
        errorMessage.clear();
    }

    parsed.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.logOutgoingMessages = messageLogConfig.enableOutgoing;

    const auto logLevelResult = document.readUnsigned(
        "zwave", "logLevel", kLogLevelMin, kLogLevelMax, std::to_string(parsed.logLevel));
    if (logLevelResult.has_value()) {
        parsed.logLevel = static_cast<std::uint8_t>(*logLevelResult);
    }

    return true;
}

[[nodiscard]] bool parseZwaveTimingSettings(
    const IniDocument& document,
    ZwaveConfig& parsed) {
    const auto pollIntervalResult = document.readUnsigned(
        "zwave",
        "pollIntervalMs",
        kPollIntervalMsMin,
        kPollIntervalMsMax,
        std::to_string(parsed.pollIntervalMs));
    if (pollIntervalResult.has_value()) {
        parsed.pollIntervalMs = static_cast<std::int64_t>(*pollIntervalResult);
    }

    const auto commandReactionPollIntervalResult = document.readUnsigned(
        "zwave",
        "commandReactionPollIntervalMs",
        kCommandReactionPollIntervalMsMin,
        kCommandReactionPollIntervalMsMax,
        std::to_string(parsed.commandReactionPollIntervalMs));
    if (commandReactionPollIntervalResult.has_value()) {
        parsed.commandReactionPollIntervalMs = static_cast<std::int64_t>(*commandReactionPollIntervalResult);
    }

    const auto commandReactionTimeoutResult = document.readUnsigned(
        "zwave",
        "commandReactionTimeoutMs",
        kCommandReactionTimeoutMsMin,
        kCommandReactionTimeoutMsMax,
        std::to_string(parsed.commandReactionTimeoutMs));
    if (commandReactionTimeoutResult.has_value()) {
        parsed.commandReactionTimeoutMs = static_cast<std::int64_t>(*commandReactionTimeoutResult);
    }

    return true;
}

[[nodiscard]] bool parseFileStoreSettings(
    const IniDocument& document,
    ZwaveConfig& parsed) {
    if (const auto fileStoreHost = document.lastValue("filestore", "host"); fileStoreHost.has_value()) {
        parsed.fileStoreHost = *fileStoreHost;
    }

    const auto fileStorePortResult = document.readUnsigned(
        "filestore", "port", 1U, 65535U, std::to_string(parsed.fileStorePort));
    if (fileStorePortResult.has_value()) {
        parsed.fileStorePort = static_cast<std::uint16_t>(*fileStorePortResult);
    }

    const auto fileStoreUseResult = document.readBool("filestore", "use", parsed.fileStoreEnabled);
    if (fileStoreUseResult.has_value()) {
        parsed.fileStoreEnabled = *fileStoreUseResult;
    }

    if (const auto settingsKeyPath = document.lastValue("filestore", "filename"); settingsKeyPath.has_value()) {
        parsed.settingsKeyPath = *settingsKeyPath;
    }

    if (const auto monitorTopicPrefix = document.lastValue("filestore", "topicPrefix");
        monitorTopicPrefix.has_value()) {
        parsed.fileStoreMonitorTopicPrefix = *monitorTopicPrefix;
    }

    const auto retryCountResult = document.readUnsigned(
        "filestore",
        "startupRetryCount",
        0U,
        std::numeric_limits<std::uint32_t>::max(),
        std::to_string(parsed.fileStoreStartupRetryCount));
    if (retryCountResult.has_value()) {
        parsed.fileStoreStartupRetryCount = static_cast<std::uint32_t>(*retryCountResult);
    }

    const auto retryIntervalResult = document.readUnsigned(
        "filestore",
        "startupRetryIntervalSeconds",
        1U,
        std::numeric_limits<std::uint32_t>::max(),
        std::to_string(parsed.fileStoreStartupRetryIntervalSeconds));
    if (retryIntervalResult.has_value()) {
        parsed.fileStoreStartupRetryIntervalSeconds = static_cast<std::uint32_t>(*retryIntervalResult);
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

struct DeviceJsonDraft {
    std::optional<std::string> topic{};
    std::optional<std::uint64_t> nodeId{};
    std::optional<std::uint64_t> classId{};
    std::optional<std::uint64_t> instance{};
    std::optional<std::uint64_t> index{};
    std::optional<std::string> type{};
    std::optional<std::string> label{};
};

[[nodiscard]] bool tryParseUnsignedJsonValue(
    const mqtt::json::JsonValue& value,
    std::uint64_t& outputValue) {
    if (!value.is_number()) {
        return false;
    }

    const double numberValue = value.as_number();
    if (numberValue < 0.0 || !std::isfinite(numberValue)) {
        return false;
    }

    const double floorValue = std::floor(numberValue);
    if (floorValue != numberValue || numberValue > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }

    outputValue = static_cast<std::uint64_t>(numberValue);
    return true;
}

[[nodiscard]] bool parseJsonDeviceField(
    const std::string& key,
    const mqtt::json::JsonValue& value,
    DeviceJsonDraft& output,
    std::string& errorMessage) {
    auto parseStringValue = [&](const char* errorPrefix, std::optional<std::string>& target) {
        if (!value.is_string()) {
            errorMessage = std::string{"invalid settings json: "} + errorPrefix;
            return false;
        }
        target = value.as_string();
        return true;
    };

    auto parseNumberFromJsonValue = [&](const char* errorPrefix, std::optional<std::uint64_t>& target) {
        std::uint64_t parsedValue = 0U;
        if (!tryParseUnsignedJsonValue(value, parsedValue)) {
            errorMessage = std::string{"invalid settings json: "} + errorPrefix;
            return false;
        }
        target = parsedValue;
        return true;
    };

    if (key == "topic") {
        return parseStringValue("device.topic must be string", output.topic);
    }
    if (key == "nodeId") {
        return parseNumberFromJsonValue("device.nodeId must be number", output.nodeId);
    }
    if (key == "classId") {
        return parseNumberFromJsonValue("device.classId must be number", output.classId);
    }
    if (key == "instance") {
        return parseNumberFromJsonValue("device.instance must be number", output.instance);
    }
    if (key == "index") {
        return parseNumberFromJsonValue("device.index must be number", output.index);
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
    const mqtt::json::JsonValue& value,
    ZwaveDeviceConfig& output,
    std::string& errorMessage) {
    if (!value.is_object()) {
        errorMessage = "invalid settings json: expected device object";
        return false;
    }

    const auto& objectValue = value.as_object();
    DeviceJsonDraft draft{};
    for (const auto& [keyText, fieldValue] : objectValue) {
        if (!parseJsonDeviceField(keyText, fieldValue, draft, errorMessage)) {
            return false;
        }
    }

    return buildDeviceConfigFromDraft(draft, output, errorMessage);
}

[[nodiscard]] bool parseJsonDevicesArray(
    const mqtt::json::JsonValue& value,
    std::vector<ZwaveDeviceConfig>& devices,
    std::string& errorMessage) {
    if (!value.is_array()) {
        errorMessage = "invalid settings json: expected devices array";
        return false;
    }

    for (const auto& entryValue : value.as_array()) {
        ZwaveDeviceConfig device{};
        if (!parseJsonDeviceObject(entryValue, device, errorMessage)) {
            return false;
        }
        devices.push_back(std::move(device));
    }

    return true;
}

[[nodiscard]] bool parseJsonRootEntry(
    const std::string& key,
    const mqtt::json::JsonValue& value,
    std::vector<ZwaveDeviceConfig>& devices,
    bool& hasDevices,
    std::string& errorMessage) {
    if (key != "devices") {
        errorMessage = "invalid settings json: unknown root key '" + key + "'";
        return false;
    }

    hasDevices = true;
    return parseJsonDevicesArray(value, devices, errorMessage);
}

[[nodiscard]] bool parseJsonRootEntries(
    const mqtt::json::JsonValue::Object& objectValue,
    std::vector<ZwaveDeviceConfig>& devices,
    bool& hasDevices,
    std::string& errorMessage) {
    for (const auto& [keyText, entryValue] : objectValue) {
        if (!parseJsonRootEntry(keyText, entryValue, devices, hasDevices, errorMessage)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool parseJsonRootDevices(
    const std::string& text,
    std::vector<ZwaveDeviceConfig>& devices,
    std::string& errorMessage) {
    devices.clear();

    mqtt::json::JsonValue rootValue{};
    try {
        rootValue = mqtt::json::JsonValue::parse(text);
    } catch (const mqtt::json::JsonException& exception) {
        errorMessage = "invalid settings json: parse failed (" + std::string{exception.what()} + ")";
        return false;
    } catch (const std::exception& exception) {
        errorMessage = "invalid settings json: parse failed (" + std::string{exception.what()} + ")";
        return false;
    }

    if (!rootValue.is_object()) {
        errorMessage = "invalid settings json: expected root object";
        return false;
    }

    bool hasDevices = false;
    if (!parseJsonRootEntries(rootValue.as_object(), devices, hasDevices, errorMessage)) {
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

[[nodiscard]] mqtt::json::JsonValue buildZwaveDeviceJsonValue(const ZwaveDeviceConfig& device) {
    mqtt::json::JsonValue::Object objectValue{};
    objectValue.emplace("topic", mqtt::json::JsonValue{device.topic});
    objectValue.emplace("nodeId", mqtt::json::JsonValue{static_cast<double>(device.nodeId)});

    if (device.classId.has_value()) {
        objectValue.emplace("classId", mqtt::json::JsonValue{static_cast<double>(*device.classId)});
    }
    if (device.instance.has_value()) {
        objectValue.emplace("instance", mqtt::json::JsonValue{static_cast<double>(*device.instance)});
    }
    if (device.index.has_value()) {
        objectValue.emplace("index", mqtt::json::JsonValue{static_cast<double>(*device.index)});
    }
    if (device.type.has_value() && !device.type->empty()) {
        objectValue.emplace("type", mqtt::json::JsonValue{*device.type});
    }
    if (device.label.has_value() && !device.label->empty()) {
        objectValue.emplace("label", mqtt::json::JsonValue{*device.label});
    }

    return mqtt::json::JsonValue{std::move(objectValue)};
}

} // namespace

bool tryLoadZwaveConfigFromIni(
    const IniDocument& document,
    ZwaveConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    ZwaveConfig parsed{};

    const auto subscribeQosResult = document.readUnsigned(
        "zwave",
        "subscribeQoS",
        0U,
        2U,
        std::to_string(static_cast<int>(parsed.subscribeQos)));
    if (subscribeQosResult.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*subscribeQosResult);
    }

    const auto publishQosResult = document.readUnsigned(
        "zwave", "qos", 0U, 2U, std::to_string(static_cast<int>(parsed.qos)));
    if (publishQosResult.has_value()) {
        parsed.qos = static_cast<Qos>(*publishQosResult);
    }

    const auto retainResult = document.readBool("zwave", "retain", parsed.retain);
    if (retainResult.has_value()) {
        parsed.retain = *retainResult;
    }

    if (!parseZwaveLoggingSettings(document, parsed, errorMessage)) {
        return false;
    }

    if (!parseZwaveTimingSettings(document, parsed)) {
        return false;
    }

    if (!parseFileStoreSettings(document, parsed)) {
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
    mqtt::json::JsonValue::Array devicesArray{};
    devicesArray.reserve(config.devices.size());
    for (const auto& device : config.devices) {
        devicesArray.push_back(buildZwaveDeviceJsonValue(device));
    }

    mqtt::json::JsonValue::Object rootObject{};
    rootObject.emplace("devices", mqtt::json::JsonValue{std::move(devicesArray)});
    return mqtt::json::JsonValue{std::move(rootObject)}.stringify();
}

bool tryLoadZwaveClientRuntimeConfigFromIni(
    const IniDocument& document,
    ZwaveClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    ZwaveClientRuntimeConfig parsed{};
    if (!tryLoadZwaveConfigFromIni(document, parsed.zwaveConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha
