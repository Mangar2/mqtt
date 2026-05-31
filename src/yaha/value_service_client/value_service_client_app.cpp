#include "yaha/value_service_client/value_service_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

void logConfigFallbackWarning(
    const std::string_view serviceName,
    const std::string_view sectionName,
    const std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText) {
    std::cerr << serviceName << "[warn] config_fallback"
              << " section=" << sectionName
              << " key=" << keyName
              << " value='" << rawValue << "'"
              << " default='" << defaultValue << "'"
              << " reason='" << reasonText << "'"
              << '\n' << std::flush;
}

} // namespace

bool tryLoadValueServiceConfigFromIni(
    const IniDocument& document,
    ValueServiceConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto keyPath = document.lastValue("filestore", "filename"); keyPath.has_value()) {
        output.valuesKeyPath = *keyPath;
    }

    if (const auto host = document.lastValue("filestore", "host"); host.has_value()) {
        output.fileStoreHost = *host;
    }

    const auto portResult = document.readUnsigned("filestore", "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "value_service_client",
            "filestore",
            "port",
            rawValue,
            std::to_string(output.fileStorePort),
            portResult.second);
    }
    if (portResult.first.has_value()) {
        output.fileStorePort = static_cast<std::uint16_t>(*portResult.first);
    }

    const auto useResult = document.readBool("filestore", "use");
    if (!useResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "use").value_or("<missing>");
        logConfigFallbackWarning(
            "value_service_client",
            "filestore",
            "use",
            rawValue,
            output.fileStoreEnabled ? "true" : "false",
            useResult.second);
    }
    if (useResult.first.has_value()) {
        output.fileStoreEnabled = *useResult.first;
    }

    const auto retryCountResult = document.readUnsigned(
        "filestore",
        "startupRetryCount",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!retryCountResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "startupRetryCount").value_or("<missing>");
        logConfigFallbackWarning(
            "value_service_client",
            "filestore",
            "startupRetryCount",
            rawValue,
            std::to_string(output.fileStoreStartupRetryCount),
            retryCountResult.second);
    }
    if (retryCountResult.first.has_value()) {
        output.fileStoreStartupRetryCount = static_cast<std::uint32_t>(*retryCountResult.first);
    }

    const auto retryIntervalResult = document.readUnsigned(
        "filestore",
        "startupRetryIntervalSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!retryIntervalResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "startupRetryIntervalSeconds").value_or("<missing>");
        logConfigFallbackWarning(
            "value_service_client",
            "filestore",
            "startupRetryIntervalSeconds",
            rawValue,
            std::to_string(output.fileStoreStartupRetryIntervalSeconds),
            retryIntervalResult.second);
    }
    if (retryIntervalResult.first.has_value()) {
        output.fileStoreStartupRetryIntervalSeconds = static_cast<std::uint32_t>(*retryIntervalResult.first);
    }

    if (const auto monitorPrefix = document.lastValue("filestore", "topicPrefix");
        monitorPrefix.has_value()) {
        output.monitorTopicPrefix = *monitorPrefix;
    }

    if (const auto legacyValuesFile = document.lastValue("valueservice", "valuesFileName");
        legacyValuesFile.has_value()) {
        output.legacyValuesFileName = *legacyValuesFile;
    }

    const auto qosResult = document.readUnsigned("valueservice", "subscribeQoS", 0U, 2U);
    if (!qosResult.second.empty()) {
        const std::string rawValue = document.lastValue("valueservice", "subscribeQoS").value_or("<missing>");
        logConfigFallbackWarning(
            "value_service_client",
            "valueservice",
            "subscribeQoS",
            rawValue,
            std::to_string(static_cast<unsigned int>(output.subscribeQos)),
            qosResult.second);
    }
    if (qosResult.first.has_value()) {
        output.subscribeQos = static_cast<Qos>(*qosResult.first);
    }

    return true;
}

bool tryLoadValueServiceClientRuntimeConfigFromIni(
    const IniDocument& document,
    ValueServiceClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    ValueServiceClientRuntimeConfig parsed{};
    if (!tryLoadValueServiceConfigFromIni(document, parsed.valueServiceConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        logConfigFallbackWarning(
            "value_service_client",
            "mqtt",
            "*",
            "<composite>",
            "defaults",
            mqttErrorMessage);
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.valueServiceConfig.logIncomingMessages,
        .enableOutgoing = parsed.valueServiceConfig.logOutgoingMessages,
        .includeReasonChain = parsed.valueServiceConfig.logReason,
    };
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = "valueservice", .key = "logIncomingMessages"},
                .outgoingEnabled = MessageLogIniBoolKey{.section = "valueservice", .key = "logOutgoingMessages"},
                .includeReasonChain = MessageLogIniBoolKey{.section = "valueservice", .key = "logReason"},
            },
            messageLogConfig,
            errorMessage)) {
        logConfigFallbackWarning(
            "value_service_client",
            "valueservice",
            "log*",
            "<composite>",
            "defaults",
            errorMessage);
        errorMessage.clear();
    }

    parsed.valueServiceConfig.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.valueServiceConfig.logOutgoingMessages = messageLogConfig.enableOutgoing;
    parsed.valueServiceConfig.logReason = messageLogConfig.includeReasonChain;
    parsed.mqttConfig.logReason = messageLogConfig.includeReasonChain;

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha
