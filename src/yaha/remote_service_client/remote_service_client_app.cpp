#include "yaha/remote_service_client/remote_service_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <iostream>
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

    if (const auto listenerHost = document.lastValue("remoteservice", "listenHost"); listenerHost.has_value()) {
        parsed.listenHost = *listenerHost;
    }

    const auto listenerPortResult = document.readUnsigned("remoteservice", "listenPort", 1U, 65535U);
    if (!listenerPortResult.second.empty()) {
        const std::string rawValue = document.lastValue("remoteservice", "listenPort").value_or("<missing>");
        logConfigFallbackWarning(
            "remote_service_client",
            "remoteservice",
            "listenPort",
            rawValue,
            std::to_string(parsed.listenPort),
            listenerPortResult.second);
    }
    if (listenerPortResult.first.has_value()) {
        parsed.listenPort = static_cast<std::uint16_t>(*listenerPortResult.first);
    }

    const auto subscribeQosResult = document.readUnsigned("remoteservice", "subscribeQoS", 0U, 2U);
    if (!subscribeQosResult.second.empty()) {
        const std::string rawValue = document.lastValue("remoteservice", "subscribeQoS").value_or("<missing>");
        logConfigFallbackWarning(
            "remote_service_client",
            "remoteservice",
            "subscribeQoS",
            rawValue,
            std::to_string(static_cast<unsigned int>(parsed.subscribeQos)),
            subscribeQosResult.second);
    }
    if (subscribeQosResult.first.has_value()) {
        parsed.subscribeQos = static_cast<Qos>(*subscribeQosResult.first);
    }

    if (const auto monitorTopicPrefix = document.lastValue("filestore", "topicPrefix");
        monitorTopicPrefix.has_value()) {
        parsed.monitorTopicPrefix = *monitorTopicPrefix;
    }

    if (!requireSetting(document, "filestore", "host", parsed.fileStoreHost, errorMessage)) {
        return false;
    }

    const auto fileStorePortResult = document.readUnsigned("filestore", "port", 1U, 65535U);
    if (!fileStorePortResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "remote_service_client",
            "filestore",
            "port",
            rawValue,
            std::to_string(parsed.fileStorePort),
            fileStorePortResult.second);
    }
    if (fileStorePortResult.first.has_value()) {
        parsed.fileStorePort = static_cast<std::uint16_t>(*fileStorePortResult.first);
    } else {
        logConfigFallbackWarning(
            "remote_service_client",
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
        logConfigFallbackWarning(
            "remote_service_client",
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