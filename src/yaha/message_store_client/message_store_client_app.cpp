#include "yaha/message_store_client/message_store_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

namespace yaha {

namespace {

constexpr std::uint64_t k_tree_uint_max{100000U};
constexpr std::uint64_t k_tree_interval_adjustment_max{100000000U};
constexpr double k_tree_factor_min{0.0};
constexpr double k_tree_factor_max{1000.0};

bool tryLoadMessageStoreSubscriptionsFromIni(
    const IniDocument& document,
    SubscriptionMap& output,
    std::string& errorMessage) {
    if (document.findSection("subscriptions") != nullptr) {
        errorMessage = "legacy section 'subscriptions' is not supported; use repeated [subscription] with topic/qos";
        return false;
    }

    const IniDocument::Section* section = document.findSection("subscription");
    if (section == nullptr || section->entries().empty()) {
        return true;
    }

    SubscriptionMap parsed{};
    std::optional<std::string> pendingTopic{};
    for (const auto& entry : section->entries()) {
        if (entry.key == "topic") {
            if (entry.value.empty()) {
                errorMessage = "subscription.topic must not be empty";
                return false;
            }
            pendingTopic = entry.value;
            continue;
        }

        if (entry.key == "qos") {
            if (!pendingTopic.has_value()) {
                errorMessage = "subscription.qos requires a preceding subscription.topic";
                return false;
            }

            const auto qosValue = IniDocument::parseUnsigned(entry.value, 0U, 2U);
            if (!qosValue.has_value()) {
                errorMessage = "invalid qos for subscription '" + *pendingTopic + "'";
                return false;
            }

            parsed[*pendingTopic] = static_cast<Qos>(*qosValue);
            pendingTopic.reset();
            continue;
        }

        errorMessage = "unknown key in [subscription]: '" + entry.key + "' (expected topic or qos)";
        return false;
    }

    if (pendingTopic.has_value()) {
        errorMessage = "subscription.topic '" + *pendingTopic + "' is missing subscription.qos";
        return false;
    }

    output = std::move(parsed);
    return true;
}

bool tryReadTreeUnsigned(const IniDocument& document,
                         const std::string& key,
                         std::uint64_t minValue,
                         std::uint64_t maxValue,
                         std::uint32_t& output,
                         std::string& errorMessage) {
    const auto readResult = document.readUnsigned("tree", key, minValue, maxValue);
    if (!readResult.second.empty()) {
        errorMessage = readResult.second;
        return false;
    }
    if (readResult.first.has_value()) {
        output = static_cast<std::uint32_t>(*readResult.first);
    }
    return true;
}

bool tryReadTreeDouble(const IniDocument& document,
                       const std::string& key,
                       double minValue,
                       double maxValue,
                       double& output,
                       std::string& errorMessage) {
    const auto configuredValue = document.lastValue("tree", key);
    if (!configuredValue.has_value()) {
        return true;
    }

    errno = 0;
    char* endPtr = nullptr;
    const double parsedValue = std::strtod(configuredValue->c_str(), &endPtr);
    if (endPtr == configuredValue->c_str() || (endPtr != nullptr && *endPtr != '\0')) {
        errorMessage = "tree." + key + " must be a floating-point number";
        return false;
    }
    if (errno == ERANGE || parsedValue < minValue || parsedValue > maxValue) {
        errorMessage = "tree." + key + " must be in range ["
            + std::to_string(minValue) + ", " + std::to_string(maxValue) + "]";
        return false;
    }

    output = parsedValue;
    return true;
}

} // namespace

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

    if (!tryReadTreeUnsigned(document,
                             "maxHistoryLength",
                             1U,
                             k_tree_uint_max,
                             output.treeConfig.maxHistoryLength,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeUnsigned(document,
                             "historyHysterese",
                             0U,
                             k_tree_uint_max,
                             output.treeConfig.historyHysterese,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeUnsigned(document,
                             "maxValuesPerHistoryEntry",
                             1U,
                             k_tree_uint_max,
                             output.treeConfig.maxValuesPerHistoryEntry,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeUnsigned(document,
                             "lengthForFurtherCompression",
                             1U,
                             k_tree_uint_max,
                             output.treeConfig.lengthForFurtherCompression,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeUnsigned(document,
                             "upperBoundAddInMilliseconds",
                             0U,
                             k_tree_interval_adjustment_max,
                             output.treeConfig.upperBoundAddInMilliseconds,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeUnsigned(document,
                             "lowerBoundSubInMilliseconds",
                             0U,
                             k_tree_interval_adjustment_max,
                             output.treeConfig.lowerBoundSubInMilliseconds,
                             errorMessage)) {
        return false;
    }
    if (!tryReadTreeDouble(document,
                           "upperBoundFactor",
                           k_tree_factor_min,
                           k_tree_factor_max,
                           output.treeConfig.upperBoundFactor,
                           errorMessage)) {
        return false;
    }
    if (!tryReadTreeDouble(document,
                           "lowerBoundFactor",
                           k_tree_factor_min,
                           k_tree_factor_max,
                           output.treeConfig.lowerBoundFactor,
                           errorMessage)) {
        return false;
    }

    SubscriptionMap parsedSubscriptions{};
    if (!tryLoadMessageStoreSubscriptionsFromIni(document, parsedSubscriptions, errorMessage)) {
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

    const auto logIncomingResult = document.readBool("messagestore", "logIncomingMessages");
    if (!logIncomingResult.second.empty()) {
        errorMessage = logIncomingResult.second;
        return false;
    }
    if (logIncomingResult.first.has_value()) {
        parsed.logIncomingMessages = *logIncomingResult.first;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
