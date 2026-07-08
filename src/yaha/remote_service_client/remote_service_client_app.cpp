#include "yaha/remote_service_client/remote_service_client_app.h"

#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

constexpr std::string_view kRemoteServiceSection{"remoteservice"};

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

bool tryLoadRemoteServiceConfigFromIni(
    const IniDocument& document,
    RemoteServiceConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    RemoteServiceConfig parsed{};

    if (const auto listenerHost = document.lastValue(kRemoteServiceSection, "listenHost"); listenerHost.has_value()) {
        parsed.listenHost = *listenerHost;
    }

    const auto listenerPortResult = document.readUnsigned(
        kRemoteServiceSection, "listenPort", 1U, 65535U, std::to_string(parsed.listenPort));
    if (listenerPortResult.has_value()) {
        parsed.listenPort = static_cast<std::uint16_t>(*listenerPortResult);
    }

    const auto subscribeQosResult = document.readUnsigned(
        kRemoteServiceSection,
        "subscribeQoS",
        0U,
        2U,
        std::to_string(static_cast<unsigned int>(parsed.subscribeQos)));
    if (subscribeQosResult.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*subscribeQosResult);
    }

    if (const auto monitorTopicPrefix = document.lastValue("filestore", "topicPrefix");
        monitorTopicPrefix.has_value()) {
        parsed.monitorTopicPrefix = *monitorTopicPrefix;
    }

    if (!requireSetting(document, "filestore", "host", parsed.fileStoreHost, errorMessage)) {
        return false;
    }

    const auto fileStorePortResult = document.readUnsigned(
        "filestore", "port", 1U, 65535U, std::to_string(parsed.fileStorePort));
    if (fileStorePortResult.has_value()) {
        parsed.fileStorePort = static_cast<std::uint16_t>(*fileStorePortResult);
    } else if (!document.lastValue("filestore", "port").has_value()) {
        document.reportFallback(
            "filestore",
            "port",
            "<missing>",
            std::to_string(parsed.fileStorePort),
            "missing required setting 'filestore.port'");
    }

    if (!requireSetting(document, "filestore", "filename", parsed.mappingKeyPath, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

bool tryLoadRemoteServiceClientRuntimeConfigFromIni(
    const IniDocument& document,
    RemoteServiceClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    RemoteServiceClientRuntimeConfig parsed{};
    if (!tryLoadRemoteServiceConfigFromIni(document, parsed.remoteServiceConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.logIncomingMessages,
        .enableOutgoing = parsed.logOutgoingMessages,
        .includeReasonChain = true,
    };
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = kRemoteServiceSection, .key = "logIncomingMessages"},
                .outgoingEnabled = MessageLogIniBoolKey{.section = kRemoteServiceSection, .key = "logOutgoingMessages"},
                .includeReasonChain = MessageLogIniBoolKey{.section = kRemoteServiceSection, .key = "logReason"},
            },
            messageLogConfig,
            errorMessage)) {
        document.reportFallback(kRemoteServiceSection, "log*", "<composite>", "defaults", errorMessage);
        errorMessage.clear();
    }

    parsed.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.logOutgoingMessages = messageLogConfig.enableOutgoing;
    parsed.mqttConfig.logReason = messageLogConfig.includeReasonChain;
    parsed.remoteServiceConfig.logOutgoingMessages = messageLogConfig.enableOutgoing;

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha