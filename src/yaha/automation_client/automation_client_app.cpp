#include "yaha/automation_client/automation_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace yaha {
namespace {

void logConfigFallbackWarning(
    std::string_view serviceName,
    std::string_view sectionName,
    std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText);

[[nodiscard]] bool tryParseDoubleText(const std::string& text, double* parsedValue) {
    char* parseEnd = nullptr;
    const double value = std::strtod(text.c_str(), &parseEnd);
    if (parseEnd == text.c_str() || *parseEnd != '\0') {
        return false;
    }
    *parsedValue = value;
    return true;
}

void applyCoordinateValueWithFallback(
    const IniDocument& document,
    const std::string_view keyName,
    double& outputValue) {
    if (const auto valueText = document.lastValue("automation", keyName);
        valueText.has_value()) {
        double parsedValue = 0.0;
        if (!tryParseDoubleText(*valueText, &parsedValue)) {
            logConfigFallbackWarning(
                "automation_client",
                "automation",
                keyName,
                *valueText,
                std::to_string(outputValue),
                "invalid floating-point value");
            return;
        }
        outputValue = parsedValue;
    }
}

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

bool tryLoadAutomationClientConfigFromIni(
    const IniDocument& document,
    AutomationClientConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto keyPath = document.lastValue("filestore", "path"); keyPath.has_value()) {
        output.rulesKeyPath = *keyPath;
    }

    if (const auto host = document.lastValue("filestore", "host"); host.has_value()) {
        output.fileStoreHost = *host;
    }

    const auto portResult = document.readUnsigned("filestore", "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "automation_client",
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
            "automation_client",
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
            "automation_client",
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
            "automation_client",
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
    } else if (const auto legacyMonitorPrefix = document.lastValue("monitoring", "topicPrefix");
               legacyMonitorPrefix.has_value()) {
        output.monitorTopicPrefix = *legacyMonitorPrefix;
    }

    if (const auto managementPrefix = document.lastValue("automation", "managementTopicPrefix");
        managementPrefix.has_value()) {
        output.managementTopicPrefix = *managementPrefix;
    }

    applyCoordinateValueWithFallback(document, "longitude", output.longitude);
    applyCoordinateValueWithFallback(document, "latitude", output.latitude);

    const auto qosResult = document.readUnsigned("automation", "subscribeQoS", 0U, 2U);
    if (!qosResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "subscribeQoS").value_or("<missing>");
        logConfigFallbackWarning(
            "automation_client",
            "automation",
            "subscribeQoS",
            rawValue,
            std::to_string(static_cast<unsigned int>(output.subscribeQos)),
            qosResult.second);
    }
    if (qosResult.first.has_value()) {
        output.subscribeQos = static_cast<Qos>(*qosResult.first);
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = output.logIncomingMessages,
        .enableOutgoing = output.logOutgoingMessages,
        .includeReasonChain = true,
    };
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = "automation", .key = "logIncomingMessages"},
                .outgoingEnabled = MessageLogIniBoolKey{.section = "automation", .key = "logOutgoingMessages"},
                .includeReasonChain = std::nullopt,
            },
            messageLogConfig,
            errorMessage)) {
        logConfigFallbackWarning(
            "automation_client",
            "automation",
            "log*",
            "<composite>",
            "defaults",
            errorMessage);
        errorMessage.clear();
    }

    output.logIncomingMessages = messageLogConfig.enableIncoming;
    output.logOutgoingMessages = messageLogConfig.enableOutgoing;

    return true;
}

bool tryLoadAutomationClientRuntimeConfigFromIni(
    const IniDocument& document,
    AutomationClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    AutomationClientRuntimeConfig parsed{};
    if (!tryLoadAutomationClientConfigFromIni(document, parsed.automationConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        logConfigFallbackWarning(
            "automation_client",
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
