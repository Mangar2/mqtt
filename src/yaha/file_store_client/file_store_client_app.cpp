#include "yaha/file_store_client/file_store_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <utility>

namespace yaha {

FileStoreConfigLoadResult loadFileStoreConfigFromIni(const IniDocument& document) {
    FileStoreConfigLoadResult result{};

    if (const auto serverHost = document.lastValue("server", "host");
        serverHost.has_value()) {
        result.config.serverHost = *serverHost;
    }

    const auto serverPortResult = document.readUnsigned("server", "port", 0U, 65535U);
    if (!serverPortResult.second.empty()) {
        result.errorMessage = serverPortResult.second;
        return result;
    }
    if (serverPortResult.first.has_value()) {
        result.config.serverPort = static_cast<std::uint16_t>(*serverPortResult.first);
    }

    if (const auto directory = document.lastValue("filestore", "directory");
        directory.has_value()) {
        result.config.directory = *directory;
    }

    const auto keepFilesResult = document.readUnsigned("filestore", "keepFiles", 1U, 1024U);
    if (!keepFilesResult.second.empty()) {
        result.errorMessage = keepFilesResult.second;
        return result;
    }
    if (keepFilesResult.first.has_value()) {
        result.config.keepFiles = static_cast<std::uint32_t>(*keepFilesResult.first);
    }

    const auto maxKeyLengthResult = document.readUnsigned("filestore", "maxKeyLength", 1U, 4096U);
    if (!maxKeyLengthResult.second.empty()) {
        result.errorMessage = maxKeyLengthResult.second;
        return result;
    }
    if (maxKeyLengthResult.first.has_value()) {
        result.config.maxKeyLength = static_cast<std::uint32_t>(*maxKeyLengthResult.first);
    }

    const auto enabledResult = document.readBool("monitoring", "enabled");
    if (!enabledResult.second.empty()) {
        result.errorMessage = enabledResult.second;
        return result;
    }
    if (enabledResult.first.has_value()) {
        result.config.monitoring.enabled = *enabledResult.first;
    }

    if (const auto topicPrefix = document.lastValue("monitoring", "topicPrefix");
        topicPrefix.has_value()) {
        result.config.monitoring.topicPrefix = *topicPrefix;
    }

    const auto qosResult = document.readUnsigned("monitoring", "qos", 0U, 2U);
    if (!qosResult.second.empty()) {
        result.errorMessage = qosResult.second;
        return result;
    }
    if (qosResult.first.has_value()) {
        result.config.monitoring.qos = static_cast<Qos>(*qosResult.first);
    }

    const auto retainResult = document.readBool("monitoring", "retain");
    if (!retainResult.second.empty()) {
        result.errorMessage = retainResult.second;
        return result;
    }
    if (retainResult.first.has_value()) {
        result.config.monitoring.retain = *retainResult.first;
    }

    const auto watchIntervalResult = document.readUnsigned("monitoring", "watchIntervalMs", 1U, 600000U);
    if (!watchIntervalResult.second.empty()) {
        result.errorMessage = watchIntervalResult.second;
        return result;
    }
    if (watchIntervalResult.first.has_value()) {
        result.config.monitoring.watchIntervalMs = static_cast<std::uint32_t>(*watchIntervalResult.first);
    }

    result.success = true;
    return result;
}

FileStoreClientRuntimeConfigLoadResult
loadFileStoreClientRuntimeConfigFromIni(const IniDocument& document) {
    FileStoreClientRuntimeConfigLoadResult result{};

    const auto storeResult = loadFileStoreConfigFromIni(document);
    if (!storeResult.success) {
        result.errorMessage = storeResult.errorMessage;
        return result;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, result.config.mqttConfig, mqttErrorMessage)) {
        result.errorMessage = mqttErrorMessage;
        return result;
    }

    result.config.storeConfig = storeResult.config;
    result.success = true;
    return result;
}

} // namespace yaha
