#include "yaha/automation_client/automation_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

namespace yaha {
namespace {

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
            document.reportFallback(
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

    const auto portResult = document.readUnsigned(
        "filestore", "port", 1U, 65535U, std::to_string(output.fileStorePort));
    if (portResult.has_value()) {
        output.fileStorePort = static_cast<std::uint16_t>(*portResult);
    }

    const auto useResult = document.readBool("filestore", "use", output.fileStoreEnabled);
    if (useResult.has_value()) {
        output.fileStoreEnabled = *useResult;
    }

    const auto retryCountResult = document.readUnsigned(
        "filestore",
        "startupRetryCount",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.fileStoreStartupRetryCount));
    if (retryCountResult.has_value()) {
        output.fileStoreStartupRetryCount = static_cast<std::uint32_t>(*retryCountResult);
    }

    const auto retryIntervalResult = document.readUnsigned(
        "filestore",
        "startupRetryIntervalSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.fileStoreStartupRetryIntervalSeconds));
    if (retryIntervalResult.has_value()) {
        output.fileStoreStartupRetryIntervalSeconds = static_cast<std::uint32_t>(*retryIntervalResult);
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

    const auto qosResult = document.readUnsigned(
        "automation",
        "subscribeQoS",
        0U,
        2U,
        std::to_string(static_cast<unsigned int>(output.subscribeQos)));
    if (qosResult.has_value()) {
        output.subscribeQos = static_cast<Qos>(*qosResult);
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
        document.reportFallback("automation", "log*", "<composite>", "defaults", errorMessage);
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
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha
