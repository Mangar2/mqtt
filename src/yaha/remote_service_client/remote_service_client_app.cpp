#include "yaha/remote_service_client/remote_service_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <utility>

namespace yaha {

namespace {

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
    RemoteServiceConfig parsed{};

    if (const auto listenerHost = document.lastValue("remoteservice", "listenHost"); listenerHost.has_value()) {
        parsed.listenHost = *listenerHost;
    }

    const auto listenerPortResult = document.readUnsigned("remoteservice", "listenPort", 1U, 65535U);
    if (!listenerPortResult.second.empty()) {
        errorMessage = listenerPortResult.second;
        return false;
    }
    if (listenerPortResult.first.has_value()) {
        parsed.listenPort = static_cast<std::uint16_t>(*listenerPortResult.first);
    }

    const auto subscribeQosResult = document.readUnsigned("remoteservice", "subscribeQoS", 0U, 2U);
    if (!subscribeQosResult.second.empty()) {
        errorMessage = subscribeQosResult.second;
        return false;
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
        errorMessage = fileStorePortResult.second;
        return false;
    }
    if (!fileStorePortResult.first.has_value()) {
        errorMessage = "missing required setting 'filestore.port'";
        return false;
    }
    parsed.fileStorePort = static_cast<std::uint16_t>(*fileStorePortResult.first);

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
    RemoteServiceClientRuntimeConfig parsed{};
    if (!tryLoadRemoteServiceConfigFromIni(document, parsed.remoteServiceConfig, errorMessage)) {
        return false;
    }
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha