#include "yaha/value_service_client/value_service_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace yaha {

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
    }

    if (const auto legacyValuesFile = document.lastValue("valueservice", "valuesFileName");
        legacyValuesFile.has_value()) {
        output.legacyValuesFileName = *legacyValuesFile;
    }

    const auto qosResult = document.readUnsigned(
        "valueservice",
        "subscribeQoS",
        0U,
        2U,
        std::to_string(static_cast<unsigned int>(output.subscribeQos)));
    if (qosResult.has_value()) {
        output.subscribeQos = static_cast<Qos>(*qosResult);
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
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
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
        document.reportFallback("valueservice", "log*", "<composite>", "defaults", errorMessage);
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
