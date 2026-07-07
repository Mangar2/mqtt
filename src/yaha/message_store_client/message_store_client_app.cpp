#include "yaha/message_store_client/message_store_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <optional>
#include <utility>

namespace yaha {

namespace {

constexpr std::uint64_t k_tree_uint_max{static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())};
constexpr std::uint64_t k_tree_interval_adjustment_max{
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())};
constexpr double k_tree_factor_min{0.0};
constexpr double k_tree_factor_max{std::numeric_limits<double>::max()};

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
    const auto readResult = document.readUnsigned("tree", key, minValue, maxValue, std::to_string(output));
    if (readResult.has_value()) {
        output = static_cast<std::uint32_t>(*readResult);
    }
    errorMessage.clear();
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
        document.reportFallback(
            "tree", key, *configuredValue, std::to_string(output), "must be a floating-point number");
        errorMessage.clear();
        return true;
    }
    if (errno == ERANGE || parsedValue < minValue || parsedValue > maxValue) {
        document.reportFallback(
            "tree", key, *configuredValue, std::to_string(output), "must be in supported range");
        errorMessage.clear();
        return true;
    }

    output = parsedValue;
    return true;
}

} // namespace

bool tryLoadMessageStoreConfigFromIni(
    const IniDocument& document,
    MessageStoreConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto cleanupTopic = document.lastValue("messagestore", "cleanupTopic");
        cleanupTopic.has_value()) {
        output.cleanupTopic = *cleanupTopic;
    }

    if (const auto replayLoadedStateFile = document.lastValue("messagestore", "replayLoadedStateFile");
        replayLoadedStateFile.has_value()) {
        output.replayLoadedStateFile = *replayLoadedStateFile;
    }

    if (const auto replayIncomingMessagesFile = document.lastValue("messagestore", "replayIncomingMessagesFile");
        replayIncomingMessagesFile.has_value()) {
        output.replayIncomingMessagesFile = *replayIncomingMessagesFile;
    }

    if (const auto serverPath = document.lastValue("server", "path");
        serverPath.has_value()) {
        output.serverPath = *serverPath;
    }

    if (const auto serverHost = document.lastValue("server", "host");
        serverHost.has_value()) {
        output.serverHost = *serverHost;
    }

    if (const auto portText = document.lastValue("server", "port"); portText.has_value()) {
        const auto parsedPort = IniDocument::parseUnsigned(*portText, 0U, 65535U);
        if (!parsedPort.has_value()) {
            errorMessage = std::format(
                "invalid unsigned value for 'server.port' (expected 0..65535, got '{}')", *portText);
            return false;
        }
        output.serverPort = static_cast<std::uint16_t>(*parsedPort);
    }

    if (const auto persistDirectory = document.lastValue("persist", "directory");
        persistDirectory.has_value()) {
        output.persistenceConfig.directory = *persistDirectory;
    }

    if (const auto persistFilename = document.lastValue("persist", "filename");
        persistFilename.has_value()) {
        output.persistenceConfig.filename = *persistFilename;
    }

    const auto intervalResult = document.readUnsigned(
        "persist",
        "intervalMs",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.persistenceConfig.intervalMs));
    if (intervalResult.has_value()) {
        output.persistenceConfig.intervalMs = static_cast<std::uint32_t>(*intervalResult);
    }

    const auto keepFilesResult = document.readUnsigned(
        "persist",
        "keepFiles",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.persistenceConfig.keepFiles));
    if (keepFilesResult.has_value()) {
        output.persistenceConfig.keepFiles = static_cast<std::uint32_t>(*keepFilesResult);
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
                             0U,
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
        document.reportFallback("subscription", "*", "<composite>", "#=1", errorMessage);
        parsedSubscriptions.clear();
        errorMessage.clear();
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
    errorMessage.clear();

    MessageStoreClientRuntimeConfig parsed{};
    if (!tryLoadMessageStoreConfigFromIni(document, parsed.storeConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        document.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.logIncomingMessages,
        .enableOutgoing = false,
        .includeReasonChain = parsed.logReason,
    };
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = "messagestore", .key = "logIncomingMessages"},
                .outgoingEnabled = std::nullopt,
                .includeReasonChain = MessageLogIniBoolKey{.section = "messagestore", .key = "logReason"},
            },
            messageLogConfig,
            errorMessage)) {
        document.reportFallback("messagestore", "log*", "<composite>", "defaults", errorMessage);
        errorMessage.clear();
    }

    parsed.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.logReason = messageLogConfig.includeReasonChain;
    parsed.mqttConfig.logReason = messageLogConfig.includeReasonChain;

    output = std::move(parsed);
    errorMessage.clear();
    return true;
}

} // namespace yaha
