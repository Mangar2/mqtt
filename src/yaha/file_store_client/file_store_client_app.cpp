#include "yaha/file_store_client/file_store_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace yaha {

FileStoreConfigLoadResult loadFileStoreConfigFromIni(const IniDocument& document) {
    FileStoreConfigLoadResult result{};

    if (const auto serverHost = document.lastValue("server", "host");
        serverHost.has_value()) {
        result.config.serverHost = *serverHost;
    }

    const auto serverPortResult = document.readUnsigned(
        "server", "port", 0U, 65535U, std::to_string(result.config.serverPort));
    if (serverPortResult.has_value()) {
        result.config.serverPort = static_cast<std::uint16_t>(*serverPortResult);
    }

    if (const auto directory = document.lastValue("filestore", "directory");
        directory.has_value()) {
        result.config.directory = *directory;
    }

    const auto keepFilesResult = document.readUnsigned(
        "filestore", "keepFiles", 1U, 1024U, std::to_string(result.config.keepFiles));
    if (keepFilesResult.has_value()) {
        result.config.keepFiles = static_cast<std::uint32_t>(*keepFilesResult);
    }

    const auto maxKeyLengthResult = document.readUnsigned(
        "filestore", "maxKeyLength", 1U, 4096U, std::to_string(result.config.maxKeyLength));
    if (maxKeyLengthResult.has_value()) {
        result.config.maxKeyLength = static_cast<std::uint32_t>(*maxKeyLengthResult);
    }

    const auto enabledResult = document.readBool(
        "monitoring", "enabled", result.config.monitoring.enabled);
    if (enabledResult.has_value()) {
        result.config.monitoring.enabled = *enabledResult;
    }

    if (const auto topicPrefix = document.lastValue("monitoring", "topicPrefix");
        topicPrefix.has_value()) {
        result.config.monitoring.topicPrefix = *topicPrefix;
    }

    const auto qosResult = document.readUnsigned(
        "monitoring",
        "qos",
        0U,
        2U,
        std::to_string(static_cast<unsigned int>(result.config.monitoring.qos)));
    if (qosResult.has_value()) {
        result.config.monitoring.qos = static_cast<Qos>(*qosResult);
    }

    const auto retainResult = document.readBool(
        "monitoring", "retain", result.config.monitoring.retain);
    if (retainResult.has_value()) {
        result.config.monitoring.retain = *retainResult;
    }

    const auto watchIntervalResult = document.readUnsigned(
        "monitoring",
        "watchIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(result.config.monitoring.watchIntervalMs));
    if (watchIntervalResult.has_value()) {
        result.config.monitoring.watchIntervalMs = static_cast<std::uint32_t>(*watchIntervalResult);
    }

    result.errorMessage.clear();
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
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    result.config.storeConfig = storeResult.config;
    result.errorMessage.clear();
    result.success = true;
    return result;
}

} // namespace yaha
