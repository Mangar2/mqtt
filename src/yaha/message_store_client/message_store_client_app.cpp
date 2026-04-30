#include "yaha/message_store_client/message_store_client_app.h"

#include "yaha/ini/ini_value_reader.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cstdint>
#include <utility>

namespace yaha {

bool tryLoadMessageStoreConfigFromIni(
    const IniDocument& document,
    MessageStoreConfig& output,
    std::string& errorMessage) {
    if (const auto cleanupTopic = iniLookupLastValue(document, "messagestore", "cleanupTopic");
        cleanupTopic.has_value()) {
        output.cleanupTopic = *cleanupTopic;
    }

    if (const auto serverPath = iniLookupLastValue(document, "server", "path");
        serverPath.has_value()) {
        output.serverPath = *serverPath;
    }

    if (const auto serverHost = iniLookupLastValue(document, "server", "host");
        serverHost.has_value()) {
        output.serverHost = *serverHost;
    }

    std::uint64_t parsed = 0U;
    if (!iniTryReadUnsigned(document,
                            "server",
                            "port",
                            0U,
                            65535U,
                            parsed,
                            "server.port",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "server", "port").has_value()) {
        output.serverPort = static_cast<std::uint16_t>(parsed);
    }

    if (const auto persistDirectory = iniLookupLastValue(document, "persist", "directory");
        persistDirectory.has_value()) {
        output.persistenceConfig.directory = *persistDirectory;
    }

    if (const auto persistFilename = iniLookupLastValue(document, "persist", "filename");
        persistFilename.has_value()) {
        output.persistenceConfig.filename = *persistFilename;
    }

    if (!iniTryReadUnsigned(document,
                            "persist",
                            "intervalMs",
                            0U,
                            86400000U,
                            parsed,
                            "persist.intervalMs",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "persist", "intervalMs").has_value()) {
        output.persistenceConfig.intervalMs = static_cast<std::uint32_t>(parsed);
    }

    if (!iniTryReadUnsigned(document,
                            "persist",
                            "keepFiles",
                            0U,
                            1024U,
                            parsed,
                            "persist.keepFiles",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "persist", "keepFiles").has_value()) {
        output.persistenceConfig.keepFiles = static_cast<std::uint32_t>(parsed);
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
