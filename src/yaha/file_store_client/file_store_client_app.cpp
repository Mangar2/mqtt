#include "yaha/file_store_client/file_store_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <iostream>
#include <limits>
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

} // namespace

FileStoreConfigLoadResult loadFileStoreConfigFromIni(const IniDocument& document) {
    FileStoreConfigLoadResult result{};

    if (const auto serverHost = document.lastValue("server", "host");
        serverHost.has_value()) {
        result.config.serverHost = *serverHost;
    }

    const auto serverPortResult = document.readUnsigned("server", "port", 0U, 65535U);
    if (!serverPortResult.second.empty()) {
        const std::string rawValue = document.lastValue("server", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "server",
            "port",
            rawValue,
            std::to_string(result.config.serverPort),
            serverPortResult.second);
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
        const std::string rawValue = document.lastValue("filestore", "keepFiles").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "filestore",
            "keepFiles",
            rawValue,
            std::to_string(result.config.keepFiles),
            keepFilesResult.second);
    }
    if (keepFilesResult.first.has_value()) {
        result.config.keepFiles = static_cast<std::uint32_t>(*keepFilesResult.first);
    }

    const auto maxKeyLengthResult = document.readUnsigned("filestore", "maxKeyLength", 1U, 4096U);
    if (!maxKeyLengthResult.second.empty()) {
        const std::string rawValue = document.lastValue("filestore", "maxKeyLength").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "filestore",
            "maxKeyLength",
            rawValue,
            std::to_string(result.config.maxKeyLength),
            maxKeyLengthResult.second);
    }
    if (maxKeyLengthResult.first.has_value()) {
        result.config.maxKeyLength = static_cast<std::uint32_t>(*maxKeyLengthResult.first);
    }

    const auto enabledResult = document.readBool("monitoring", "enabled");
    if (!enabledResult.second.empty()) {
        const std::string rawValue = document.lastValue("monitoring", "enabled").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "monitoring",
            "enabled",
            rawValue,
            result.config.monitoring.enabled ? "true" : "false",
            enabledResult.second);
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
        const std::string rawValue = document.lastValue("monitoring", "qos").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "monitoring",
            "qos",
            rawValue,
            std::to_string(static_cast<unsigned int>(result.config.monitoring.qos)),
            qosResult.second);
    }
    if (qosResult.first.has_value()) {
        result.config.monitoring.qos = static_cast<Qos>(*qosResult.first);
    }

    const auto retainResult = document.readBool("monitoring", "retain");
    if (!retainResult.second.empty()) {
        const std::string rawValue = document.lastValue("monitoring", "retain").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "monitoring",
            "retain",
            rawValue,
            result.config.monitoring.retain ? "true" : "false",
            retainResult.second);
    }
    if (retainResult.first.has_value()) {
        result.config.monitoring.retain = *retainResult.first;
    }

    const auto watchIntervalResult = document.readUnsigned(
        "monitoring",
        "watchIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!watchIntervalResult.second.empty()) {
        const std::string rawValue = document.lastValue("monitoring", "watchIntervalMs").value_or("<missing>");
        logConfigFallbackWarning(
            "file_store_client",
            "monitoring",
            "watchIntervalMs",
            rawValue,
            std::to_string(result.config.monitoring.watchIntervalMs),
            watchIntervalResult.second);
    }
    if (watchIntervalResult.first.has_value()) {
        result.config.monitoring.watchIntervalMs = static_cast<std::uint32_t>(*watchIntervalResult.first);
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
        logConfigFallbackWarning(
            "file_store_client",
            "mqtt",
            "*",
            "<composite>",
            "defaults",
            mqttErrorMessage);
    }

    result.config.storeConfig = storeResult.config;
    result.errorMessage.clear();
    result.success = true;
    return result;
}

} // namespace yaha
