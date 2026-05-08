#include "yaha/value_service_client/value_service_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <utility>

namespace yaha {

bool tryLoadValueServiceConfigFromIni(
    const IniDocument& document,
    ValueServiceConfig& output,
    std::string& errorMessage) {
    if (const auto keyPath = document.lastValue("filestore", "filename"); keyPath.has_value()) {
        output.valuesKeyPath = *keyPath;
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
        errorMessage = qosResult.second;
        return false;
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
    ValueServiceClientRuntimeConfig parsed{};
    if (!tryLoadValueServiceConfigFromIni(document, parsed.valueServiceConfig, errorMessage)) {
        return false;
    }
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
