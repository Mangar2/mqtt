#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/message/message_log_service.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr std::uint64_t k_milliseconds_per_second{1000U};

struct SubscriptionMapLoadResult {
    std::optional<SubscriptionMap> subscriptions{};
    std::string errorMessage{};
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] SubscriptionMapLoadResult tryLoadStructuredSourceSubscriptionsFromIni(
    const IniDocument& document) {
    const IniDocument::Section* section = document.findSection("subscription");
    if (section == nullptr || section->entries().empty()) {
        return {.subscriptions = SubscriptionMap{}, .errorMessage = ""};
    }

    SubscriptionMap parsed{};
    std::optional<std::string> pendingTopic{};
    std::optional<Qos> pendingQos{};

    const auto flushPending = [&]() -> std::optional<std::string> {
        if (!pendingTopic.has_value() && !pendingQos.has_value()) {
            return std::nullopt;
        }

        if (!pendingTopic.has_value() || !pendingQos.has_value()) {
            return std::string{"incomplete [subscription] entry (expected topic and qos)"};
        }

        parsed[*pendingTopic] = *pendingQos;
        pendingTopic.reset();
        pendingQos.reset();
        return std::nullopt;
    };

    for (const auto& entry : section->entries()) {
        if (entry.key == "topic") {
            if (const auto maybeError = flushPending(); maybeError.has_value()) {
                return {.subscriptions = std::nullopt, .errorMessage = *maybeError};
            }

            if (entry.value.empty()) {
                return {
                    .subscriptions = std::nullopt,
                    .errorMessage = "invalid topic in [subscription] (topic must not be empty)"};
            }

            pendingTopic = entry.value;
            continue;
        }

        if (entry.key == "qos") {
            if (!pendingTopic.has_value()) {
                return {
                    .subscriptions = std::nullopt,
                    .errorMessage = "invalid [subscription] entry (qos requires topic first)"};
            }
            if (pendingQos.has_value()) {
                return {
                    .subscriptions = std::nullopt,
                    .errorMessage = "invalid [subscription] entry (duplicate qos key)"};
            }

            const auto qosValue = IniDocument::parseUnsigned(entry.value, 0U, 2U);
            if (!qosValue.has_value()) {
                return {
                    .subscriptions = std::nullopt,
                    .errorMessage = "invalid qos for subscription '" + *pendingTopic + "'"};
            }

            pendingQos = static_cast<Qos>(*qosValue);
            continue;
        }

        return {
            .subscriptions = std::nullopt,
            .errorMessage =
                "invalid key in [subscription] (expected 'topic' or 'qos', got '" + entry.key + "')"};
    }

    if (const auto maybeError = flushPending(); maybeError.has_value()) {
        return {.subscriptions = std::nullopt, .errorMessage = *maybeError};
    }

    return {.subscriptions = std::move(parsed), .errorMessage = ""};
}

} // namespace

SourceHttpBrokerConfigLoadResult tryLoadSourceHttpBrokerConfigFromIni(
    const IniDocument& document) {
    SourceHttpBrokerConfig parsed{};

    if (const auto host = document.lastValue("sourceHttpBroker", "host"); host.has_value()) {
        parsed.brokerHost = *host;
    }

    if (const auto clientId = document.lastValue("sourceHttpBroker", "clientId");
        clientId.has_value()) {
        parsed.clientId = *clientId;
    }

    if (const auto listenerHost = document.lastValue("sourceHttpBroker", "listenerHost");
        listenerHost.has_value()) {
        parsed.listenerHost = *listenerHost;
    }

    if (const auto listenerBindHost = document.lastValue("sourceHttpBroker", "listenerBindHost");
        listenerBindHost.has_value()) {
        parsed.listenerBindHost = *listenerBindHost;
    }

    const auto sourcePortResult = document.readUnsigned(
        "sourceHttpBroker", "port", 1U, 65535U, std::to_string(parsed.brokerPort));
    if (sourcePortResult.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*sourcePortResult);
    }

    const auto listenerPortResult = document.readUnsigned(
        "sourceHttpBroker", "listenerPort", 0U, 65535U, std::to_string(parsed.listenerPort));
    if (listenerPortResult.has_value()) {
        parsed.listenerPort = static_cast<std::uint16_t>(*listenerPortResult);
    }

    const auto keepAliveResult = document.readUnsigned(
        "sourceHttpBroker",
        "keepAliveSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.keepAliveSeconds));
    if (keepAliveResult.has_value()) {
        parsed.keepAliveSeconds = static_cast<std::uint32_t>(*keepAliveResult);
    }

    const auto cleanResult = document.readBool("sourceHttpBroker", "clean", parsed.clean);
    if (cleanResult.has_value()) {
        parsed.clean = *cleanResult;
    }

    SubscriptionMap parsedSubscriptions{};
    const auto structuredSubscriptionsResult = tryLoadStructuredSourceSubscriptionsFromIni(document);
    if (!structuredSubscriptionsResult.subscriptions.has_value()) {
        document.reportFallback(
            "subscription", "*", "<composite>", "#=1", structuredSubscriptionsResult.errorMessage);
    } else {
        parsedSubscriptions = *structuredSubscriptionsResult.subscriptions;
    }

    if (parsedSubscriptions.empty()) {
        parsed.subscribeTopics = {{"#", Qos::AtLeastOnce}};
    } else {
        parsed.subscribeTopics = std::move(parsedSubscriptions);
    }

    return {.config = std::move(parsed), .errorMessage = ""};
}

ReceiverMqttBrokerConfigLoadResult tryLoadReceiverMqttBrokerConfigFromIni(
    const IniDocument& document) {
    YahaMqttClient::Config parsed{
        .brokerHost = "127.0.0.1",
        .clientId = "broker-connector-receiver",
        .enableLifecycleTrace = true,
        .enableMessageTrace = true};

    if (const auto host = document.lastValue("receiverMqttBroker", "host"); host.has_value()) {
        parsed.brokerHost = *host;
    }

    if (const auto clientId = document.lastValue("receiverMqttBroker", "clientId");
        clientId.has_value()) {
        parsed.clientId = *clientId;
    }

    const auto receiverPortResult = document.readUnsigned(
        "receiverMqttBroker", "port", 1U, 65535U, std::to_string(parsed.brokerPort));
    if (receiverPortResult.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*receiverPortResult);
    }

    const auto reconnectDelayResult = document.readUnsigned(
        "receiverMqttBroker",
        "reconnectDelayMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.reconnectDelay.count()));
    if (reconnectDelayResult.has_value()) {
        parsed.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult};
    }

    const auto keepAliveSecondsResult = document.readUnsigned(
        "receiverMqttBroker",
        "keepAliveSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.keepAliveInterval.count() / static_cast<long long>(k_milliseconds_per_second)));
    if (keepAliveSecondsResult.has_value()) {
        parsed.keepAliveInterval = std::chrono::milliseconds{
            *keepAliveSecondsResult * k_milliseconds_per_second};
    }

    const auto loopSleepResult = document.readUnsigned(
        "receiverMqttBroker",
        "loopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.loopSleep.count()));
    if (loopSleepResult.has_value()) {
        parsed.loopSleep = std::chrono::milliseconds{*loopSleepResult};
    }

    const auto lifecycleTraceResult = document.readBool(
        "receiverMqttBroker", "enableLifecycleTrace", parsed.enableLifecycleTrace);
    if (lifecycleTraceResult.has_value()) {
        parsed.enableLifecycleTrace = *lifecycleTraceResult;
    }

    return {.config = std::move(parsed), .errorMessage = ""};
}

void applyMonitoringAndMessageLogConfig(
    const IniDocument& document,
    BrokerConnectorClientRuntimeConfig& parsed) {
    const auto sourceTraceResult = document.readBool(
        "monitoring", "sourceLifecycleTrace", parsed.sourceLifecycleConfig.enableTrace);
    if (sourceTraceResult.has_value()) {
        parsed.sourceLifecycleConfig.enableTrace = *sourceTraceResult;
    }

    MessageLogConfig messageLogConfig{
        .enableIncoming = parsed.sourceConfig.logIncomingMessages,
        .enableOutgoing = parsed.receiverConfig.enableMessageTrace,
        .includeReasonChain = true,
    };
    std::string messageLogConfigError{};
    if (!tryLoadMessageLogConfigFromIni(
            document,
            MessageLogIniKeys{
                .incomingEnabled = MessageLogIniBoolKey{.section = "monitoring", .key = "logIncomingMessage"},
                .outgoingEnabled = MessageLogIniBoolKey{.section = "monitoring", .key = "logOutgoingMessage"},
                .includeReasonChain = std::nullopt,
            },
            messageLogConfig,
            messageLogConfigError)) {
        document.reportFallback("monitoring", "log*", "<composite>", "defaults", messageLogConfigError);
    }

    parsed.sourceConfig.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.receiverConfig.enableMessageTrace = messageLogConfig.enableOutgoing;
    parsed.relayPolicyConfig.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.relayPolicyConfig.logOutgoingMessages = messageLogConfig.enableOutgoing;
}

BrokerConnectorClientRuntimeConfigLoadResult tryLoadBrokerConnectorClientRuntimeConfigFromIni(
    const IniDocument& document) {
    BrokerConnectorClientRuntimeConfig parsed{};

    const auto sourceResult = tryLoadSourceHttpBrokerConfigFromIni(document);
    if (!sourceResult.config.has_value()) {
        return {.config = std::nullopt, .errorMessage = sourceResult.errorMessage};
    }
    parsed.sourceConfig = *sourceResult.config;

    const auto receiverResult = tryLoadReceiverMqttBrokerConfigFromIni(document);
    if (!receiverResult.config.has_value()) {
        return {.config = std::nullopt, .errorMessage = receiverResult.errorMessage};
    }
    parsed.receiverConfig = *receiverResult.config;

    const auto sourceReconnectResult = document.readUnsigned(
        "automation",
        "reconnectDelayMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.sourceLifecycleConfig.reconnectDelay.count()));
    if (sourceReconnectResult.has_value()) {
        parsed.sourceLifecycleConfig.reconnectDelay = std::chrono::milliseconds{*sourceReconnectResult};
    }

    const auto sourceLoopSleepResult = document.readUnsigned(
        "automation",
        "sourceLoopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.sourceLifecycleConfig.loopSleep.count()));
    if (sourceLoopSleepResult.has_value()) {
        parsed.sourceLifecycleConfig.loopSleep = std::chrono::milliseconds{*sourceLoopSleepResult};
    }

    const auto sourceKeepAliveResult = document.readUnsigned(
        "automation",
        "sourceKeepAliveIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.sourceLifecycleConfig.keepAliveInterval.count()));
    if (sourceKeepAliveResult.has_value()) {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{*sourceKeepAliveResult};
    } else {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{
            static_cast<std::uint64_t>(parsed.sourceConfig.keepAliveSeconds) * k_milliseconds_per_second};
    }

    const auto maxRetryResult = document.readUnsigned(
        "automation",
        "maxPublishRetries",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.relayPolicyConfig.maxPublishRetries));
    if (maxRetryResult.has_value()) {
        parsed.relayPolicyConfig.maxPublishRetries = static_cast<std::uint32_t>(*maxRetryResult);
    }

    const auto backoffResult = document.readUnsigned(
        "automation",
        "publishRetryBackoffMs",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(parsed.relayPolicyConfig.publishRetryBackoff.count()));
    if (backoffResult.has_value()) {
        parsed.relayPolicyConfig.publishRetryBackoff = std::chrono::milliseconds{*backoffResult};
    }

    const auto normalizeQosResult = document.readBool(
        "automation", "normalizeQosToAtLeastOnce", parsed.relayPolicyConfig.normalizeQosToAtLeastOnce);
    if (normalizeQosResult.has_value()) {
        parsed.relayPolicyConfig.normalizeQosToAtLeastOnce = *normalizeQosResult;
    }

    const auto retainResult = document.readBool(
        "automation", "retainPassthrough", parsed.relayPolicyConfig.retainPassthrough);
    if (retainResult.has_value()) {
        parsed.relayPolicyConfig.retainPassthrough = *retainResult;
    }

    applyMonitoringAndMessageLogConfig(document, parsed);

    return {.config = std::move(parsed), .errorMessage = ""};
}

} // namespace yaha
