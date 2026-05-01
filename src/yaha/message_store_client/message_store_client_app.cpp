#include "yaha/message_store_client/message_store_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <utility>

namespace yaha {

bool tryLoadMessageStoreConfigFromIni(
    const IniDocument& document,
    MessageStoreConfig& output,
    std::string& errorMessage) {
    if (const auto cleanupTopic = document.lastValue("messagestore", "cleanupTopic");
        cleanupTopic.has_value()) {
        output.cleanupTopic = *cleanupTopic;
    }

    if (const auto serverPath = document.lastValue("server", "path");
        serverPath.has_value()) {
        output.serverPath = *serverPath;
    }

    if (const auto serverHost = document.lastValue("server", "host");
        serverHost.has_value()) {
        output.serverHost = *serverHost;
    }

    const auto serverPortResult = document.readUnsigned("server", "port", 0U, 65535U);
    if (!serverPortResult.second.empty()) {
        errorMessage = serverPortResult.second;
        return false;
    }
    if (serverPortResult.first.has_value()) {
        output.serverPort = static_cast<std::uint16_t>(*serverPortResult.first);
    }

    if (const auto persistDirectory = document.lastValue("persist", "directory");
        persistDirectory.has_value()) {
        output.persistenceConfig.directory = *persistDirectory;
    }

    if (const auto persistFilename = document.lastValue("persist", "filename");
        persistFilename.has_value()) {
        output.persistenceConfig.filename = *persistFilename;
    }

    const auto intervalResult = document.readUnsigned("persist", "intervalMs", 0U, 86400000U);
    if (!intervalResult.second.empty()) {
        errorMessage = intervalResult.second;
        return false;
    }
    if (intervalResult.first.has_value()) {
        output.persistenceConfig.intervalMs = static_cast<std::uint32_t>(*intervalResult.first);
    }

    const auto keepFilesResult = document.readUnsigned("persist", "keepFiles", 0U, 1024U);
    if (!keepFilesResult.second.empty()) {
        errorMessage = keepFilesResult.second;
        return false;
    }
    if (keepFilesResult.first.has_value()) {
        output.persistenceConfig.keepFiles = static_cast<std::uint32_t>(*keepFilesResult.first);
    }

    SubscriptionMap parsedSubscriptions{};
    if (!tryLoadSubscriptionsFromIni(document, "subscriptions", parsedSubscriptions, errorMessage)) {
        return false;
    }

    if (parsedSubscriptions.empty()) {
        output.subscriptions = {{"#", Qos::AtLeastOnce}};
    } else {
        output.subscriptions = std::move(parsedSubscriptions);
    }

    return true;
}

bool tryLoadMessageStoreClientRuntimeConfigFromIni(
    const IniDocument& document,
    MessageStoreClientRuntimeConfig& output,
    std::string& errorMessage) {
    MessageStoreClientRuntimeConfig parsed{};
    if (!tryLoadMessageStoreConfigFromIni(document, parsed.storeConfig, errorMessage)) {
        return false;
    }
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
