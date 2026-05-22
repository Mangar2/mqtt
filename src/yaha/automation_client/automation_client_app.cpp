#include "yaha/automation_client/automation_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <cstdlib>
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

} // namespace

bool tryLoadAutomationClientConfigFromIni(
    const IniDocument& document,
    AutomationClientConfig& output,
    std::string& errorMessage) {
    if (const auto keyPath = document.lastValue("filestore", "path"); keyPath.has_value()) {
        output.rulesKeyPath = *keyPath;
    }

    if (const auto host = document.lastValue("filestore", "host"); host.has_value()) {
        output.fileStoreHost = *host;
    }

    const auto portResult = document.readUnsigned("filestore", "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        errorMessage = portResult.second;
        return false;
    }
    if (portResult.first.has_value()) {
        output.fileStorePort = static_cast<std::uint16_t>(*portResult.first);
    }

    const auto useResult = document.readBool("filestore", "use");
    if (!useResult.second.empty()) {
        errorMessage = useResult.second;
        return false;
    }
    if (useResult.first.has_value()) {
        output.fileStoreEnabled = *useResult.first;
    }

    const auto retryCountResult = document.readUnsigned("filestore", "startupRetryCount", 0U, 1000U);
    if (!retryCountResult.second.empty()) {
        errorMessage = retryCountResult.second;
        return false;
    }
    if (retryCountResult.first.has_value()) {
        output.fileStoreStartupRetryCount = static_cast<std::uint32_t>(*retryCountResult.first);
    }

    const auto retryIntervalResult = document.readUnsigned("filestore", "startupRetryIntervalSeconds", 1U, 3600U);
    if (!retryIntervalResult.second.empty()) {
        errorMessage = retryIntervalResult.second;
        return false;
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

    if (const auto longitudeValue = document.lastValue("automation", "longitude");
        longitudeValue.has_value()) {
        double parsedLongitude = 0.0;
        if (!tryParseDoubleText(*longitudeValue, &parsedLongitude)) {
            errorMessage = "invalid value for automation.longitude";
            return false;
        }
        output.longitude = parsedLongitude;
    }

    if (const auto latitudeValue = document.lastValue("automation", "latitude");
        latitudeValue.has_value()) {
        double parsedLatitude = 0.0;
        if (!tryParseDoubleText(*latitudeValue, &parsedLatitude)) {
            errorMessage = "invalid value for automation.latitude";
            return false;
        }
        output.latitude = parsedLatitude;
    }

    const auto qosResult = document.readUnsigned("automation", "subscribeQoS", 0U, 2U);
    if (!qosResult.second.empty()) {
        errorMessage = qosResult.second;
        return false;
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
        return false;
    }

    output.logIncomingMessages = messageLogConfig.enableIncoming;
    output.logOutgoingMessages = messageLogConfig.enableOutgoing;

    return true;
}

bool tryLoadAutomationClientRuntimeConfigFromIni(
    const IniDocument& document,
    AutomationClientRuntimeConfig& output,
    std::string& errorMessage) {
    AutomationClientRuntimeConfig parsed{};
    if (!tryLoadAutomationClientConfigFromIni(document, parsed.automationConfig, errorMessage)) {
        return false;
    }

    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
