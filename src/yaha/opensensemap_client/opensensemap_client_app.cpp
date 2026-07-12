#include "yaha/opensensemap_client/opensensemap_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <cstdio>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::string_view k_open_sense_map_section{"opensensemap"};
constexpr std::string_view k_sensor_section{"sensor"};
constexpr std::size_t kCommandReadBufferSize{256U};

struct SensorAssembly {
    std::optional<std::string> sensorName{};
    std::optional<std::string> sensorUnit{};
    std::optional<std::string> topicFilter{};
    std::optional<std::string> sensorIdentifier{};
    std::optional<std::uint32_t> minUploadIntervalSeconds{};

    [[nodiscard]] bool empty() const {
        return !sensorName.has_value() && !sensorUnit.has_value()
            && !topicFilter.has_value() && !sensorIdentifier.has_value();
    }

    [[nodiscard]] bool complete() const {
        return sensorName.has_value() && sensorUnit.has_value()
            && topicFilter.has_value() && sensorIdentifier.has_value();
    }
};

[[nodiscard]] bool flushPendingSensor(
    SensorAssembly& pending,
    std::vector<OpenSenseMapSensorConfig>& sensors,
    std::string& errorMessage) {
    if (pending.empty()) {
        return true;
    }

    if (!pending.complete()) {
        errorMessage = "incomplete [sensor] entry (required keys: name, unit, topic, id)";
        return false;
    }

    sensors.push_back(OpenSenseMapSensorConfig{
        .sensorName = *pending.sensorName,
        .sensorUnit = *pending.sensorUnit,
        .topicFilter = *pending.topicFilter,
        .sensorIdentifier = *pending.sensorIdentifier,
        .minUploadIntervalSeconds = pending.minUploadIntervalSeconds.value_or(0U),
    });
    pending = SensorAssembly{};
    return true;
}

[[nodiscard]] bool tryApplySensorEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::vector<OpenSenseMapSensorConfig>& sensors,
    std::string& errorMessage);

[[nodiscard]] bool applySensorUnitEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::string& errorMessage) {
    if (entry.value.empty()) {
        errorMessage = "sensor.unit must not be empty";
        return false;
    }
    if (pending.sensorUnit.has_value()) {
        errorMessage = "duplicate sensor.unit in one [sensor] entry";
        return false;
    }
    pending.sensorUnit = entry.value;
    return true;
}

[[nodiscard]] bool applySensorTopicEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::string& errorMessage) {
    if (entry.value.empty()) {
        errorMessage = "sensor.topic must not be empty";
        return false;
    }
    if (pending.topicFilter.has_value()) {
        errorMessage = "duplicate sensor.topic in one [sensor] entry";
        return false;
    }
    pending.topicFilter = entry.value;
    return true;
}

[[nodiscard]] bool applySensorIdEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::string& errorMessage) {
    if (entry.value.empty()) {
        errorMessage = "sensor.id must not be empty";
        return false;
    }
    if (pending.sensorIdentifier.has_value()) {
        errorMessage = "duplicate sensor.id in one [sensor] entry";
        return false;
    }
    pending.sensorIdentifier = entry.value;
    return true;
}

[[nodiscard]] bool applySensorMinUploadIntervalEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::string& errorMessage) {
    if (pending.minUploadIntervalSeconds.has_value()) {
        errorMessage = "duplicate sensor.minUploadIntervalSeconds in one [sensor] entry";
        return false;
    }

    const auto parsedValue = IniDocument::parseUnsigned(
        entry.value,
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!parsedValue.has_value()) {
        errorMessage = "sensor.minUploadIntervalSeconds must be in range 0..4294967295";
        return false;
    }

    pending.minUploadIntervalSeconds = static_cast<std::uint32_t>(*parsedValue);
    return true;
}

[[nodiscard]] bool tryApplySensorEntry(
    const IniDocument::Entry& entry,
    SensorAssembly& pending,
    std::vector<OpenSenseMapSensorConfig>& sensors,
    std::string& errorMessage) {
    if (entry.key == "name") {
        if (!flushPendingSensor(pending, sensors, errorMessage)) {
            return false;
        }
        if (entry.value.empty()) {
            errorMessage = "sensor.name must not be empty";
            return false;
        }
        pending.sensorName = entry.value;
        return true;
    }

    if (!pending.sensorName.has_value()) {
        errorMessage = "sensor fields require preceding sensor.name";
        return false;
    }

    if (entry.key == "unit") {
        return applySensorUnitEntry(entry, pending, errorMessage);
    }

    if (entry.key == "topic") {
        return applySensorTopicEntry(entry, pending, errorMessage);
    }

    if (entry.key == "id") {
        return applySensorIdEntry(entry, pending, errorMessage);
    }

    if (entry.key == "minUploadIntervalSeconds") {
        return applySensorMinUploadIntervalEntry(entry, pending, errorMessage);
    }

    if (entry.key == "uint") {
        errorMessage = "invalid key in [sensor]: 'uint' (use 'unit')";
        return false;
    }

    errorMessage = "invalid key in [sensor] (expected name, unit, topic, id, minUploadIntervalSeconds; got '" + entry.key + "')";
    return false;
}

[[nodiscard]] std::string shellQuote(const std::string& rawText) {
    std::string quotedText{"'"};
    for (const char currentChar : rawText) {
        if (currentChar == '\'') {
            quotedText += "'\\''";
        } else {
            quotedText.push_back(currentChar);
        }
    }
    quotedText.push_back('\'');
    return quotedText;
}

[[nodiscard]] std::pair<int, std::string> executeCommand(const std::string& commandText) {
    FILE* processHandle = popen(commandText.c_str(), "r");
    if (processHandle == nullptr) {
        throw std::runtime_error("failed to start curl process");
    }

    std::string outputText{};
    std::array<char, kCommandReadBufferSize> readBuffer{};
    while (fgets(readBuffer.data(), static_cast<int>(readBuffer.size()), processHandle) != nullptr) {
        outputText += readBuffer.data();
    }

    const int exitStatus = pclose(processHandle);
    return {exitStatus, std::move(outputText)};
}

[[nodiscard]] OpenSenseMapHttpResult parseCurlOutput(const std::string& outputText) {
    const std::string statusMarker{"\n__YAHA_STATUS__:"};
    const std::string contentTypeMarker{"\n__YAHA_CTYPE__:"};

    const std::size_t statusPosition = outputText.rfind(statusMarker);
    const std::size_t contentTypePosition = outputText.rfind(contentTypeMarker);
    if (statusPosition == std::string::npos || contentTypePosition == std::string::npos
        || contentTypePosition <= statusPosition) {
        throw std::runtime_error("failed to parse curl response metadata");
    }

    const std::string bodyText = outputText.substr(0U, statusPosition);
    const std::size_t statusValueStart = statusPosition + statusMarker.size();
    const std::size_t statusValueEnd = contentTypePosition;
    const std::string statusText = outputText.substr(statusValueStart, statusValueEnd - statusValueStart);

    int statusCode = 0;
    try {
        statusCode = std::stoi(statusText);
    } catch (...) {
        throw std::runtime_error("failed to parse HTTP status code from curl output");
    }

    std::string contentType = outputText.substr(contentTypePosition + contentTypeMarker.size());
    while (!contentType.empty() && (contentType.back() == '\n' || contentType.back() == '\r')) {
        contentType.pop_back();
    }

    return OpenSenseMapHttpResult{
        .statusCode = statusCode,
        .payload = bodyText,
        .contentType = contentType,
    };
}

[[nodiscard]] bool tryLoadSensorMappingsFromIni(
    const IniDocument& document,
    std::vector<OpenSenseMapSensorConfig>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection(k_sensor_section);
    if (section == nullptr || section->entries().empty()) {
        errorMessage = "missing [sensor] entries";
        return false;
    }

    std::vector<OpenSenseMapSensorConfig> parsedSensors{};
    SensorAssembly pending{};

    for (const auto& entry : section->entries()) {
        if (!tryApplySensorEntry(entry, pending, parsedSensors, errorMessage)) {
            return false;
        }
    }

    if (!flushPendingSensor(pending, parsedSensors, errorMessage)) {
        return false;
    }

    if (parsedSensors.empty()) {
        errorMessage = "missing [sensor] entries";
        return false;
    }

    output = std::move(parsedSensors);
    return true;
}

} // namespace

bool tryLoadOpenSenseMapConfigFromIni(
    const IniDocument& document,
    OpenSenseMapConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto stationName = document.lastValue(k_open_sense_map_section, "station");
        stationName.has_value()) {
        output.stationName = *stationName;
    }

    if (const auto boxIdentifier = document.lastValue(k_open_sense_map_section, "id");
        boxIdentifier.has_value()) {
        output.boxIdentifier = *boxIdentifier;
    }

    if (const auto host = document.lastValue(k_open_sense_map_section, "host"); host.has_value()) {
        output.host = *host;
    }

    const auto portResult = document.readUnsigned(
        k_open_sense_map_section, "port", 1U, 65535U, std::to_string(output.port));
    if (portResult.has_value()) {
        output.port = static_cast<std::uint16_t>(*portResult);
    }

    const auto qosResult = document.readUnsigned(
        k_open_sense_map_section,
        "qos",
        0U,
        2U,
        std::to_string(static_cast<unsigned int>(output.subscribeQos)));
    if (qosResult.has_value()) {
        output.subscribeQos = static_cast<Qos>(*qosResult);
    }

    const auto useTlsResult = document.readBool(k_open_sense_map_section, "useTls", output.useTls);
    if (useTlsResult.has_value()) {
        output.useTls = *useTlsResult;
    }

    if (output.boxIdentifier.empty()) {
        errorMessage = "opensensemap.id must not be empty";
        return false;
    }

    std::vector<OpenSenseMapSensorConfig> sensorMappings{};
    if (!tryLoadSensorMappingsFromIni(document, sensorMappings, errorMessage)) {
        return false;
    }

    output.sensors = std::move(sensorMappings);
    return true;
}

bool tryLoadOpenSenseMapClientRuntimeConfigFromIni(
    const IniDocument& document,
    OpenSenseMapClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    OpenSenseMapClientRuntimeConfig parsed{};
    if (!tryLoadOpenSenseMapConfigFromIni(document, parsed.openSenseMapConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.openSenseMapConfig.logIncomingMessages,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };
    std::string messageLogConfigError{};
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = k_open_sense_map_section, .key = "logIncomingMessages"},
                .includeReasonChain = MessageLogIniBoolKey{.section = k_open_sense_map_section, .key = "logReason"},
            },
            messageLogConfig,
            messageLogConfigError)) {
        document.reportFallback(
            k_open_sense_map_section, "*", "<composite>", "defaults", messageLogConfigError);
    }

    parsed.openSenseMapConfig.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.mqttConfig.logReason = messageLogConfig.includeReasonChain;

    output = std::move(parsed);
    return true;
}

OpenSenseMapRequestSender makeOpenSenseMapRequestSender(const OpenSenseMapConfig& config) {
    return makeOpenSenseMapRequestSender(config, executeCommand);
}

OpenSenseMapRequestSender makeOpenSenseMapRequestSender(
    const OpenSenseMapConfig& config,
    std::function<std::pair<int, std::string>(const std::string&)> commandExecutor) {
    return [host = config.host,
            port = config.port,
            useTls = config.useTls,
            commandExecutor = std::move(commandExecutor)](
               const std::string& requestPath,
               const std::string& requestPayload) {
        const std::string scheme = useTls ? "https" : "http";
        const std::string targetUrl =
            scheme + "://" + host + ":" + std::to_string(port) + requestPath;

        const std::string commandText =
            "curl --silent --show-error --max-time 5 --request POST"
            " --header " + shellQuote("content-type: application/json; charset=UTF-8") +
            " --data " + shellQuote(requestPayload) +
            " --write-out " +
            shellQuote("\n__YAHA_STATUS__:%{http_code}\n__YAHA_CTYPE__:%{content_type}") +
            " " + shellQuote(targetUrl);

        const auto [exitStatus, outputText] = commandExecutor(commandText);
        if (exitStatus != 0) {
            throw std::runtime_error("curl request execution failed");
        }

        return parseCurlOutput(outputText);
    };
}

} // namespace yaha
