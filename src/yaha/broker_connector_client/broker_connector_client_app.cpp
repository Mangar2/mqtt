#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/message/message_log_service.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

constexpr std::uint64_t k_milliseconds_per_second{1000U};

struct SubscriptionMapLoadResult {
    std::optional<SubscriptionMap> subscriptions{};
    std::string errorMessage{};
};

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

    const auto sourcePortResult = document.readUnsigned("sourceHttpBroker", "port", 1U, 65535U);
    if (!sourcePortResult.second.empty()) {
        const std::string rawValue = document.lastValue("sourceHttpBroker", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "sourceHttpBroker",
            "port",
            rawValue,
            std::to_string(parsed.brokerPort),
            sourcePortResult.second);
    }
    if (sourcePortResult.first.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*sourcePortResult.first);
    }

    const auto listenerPortResult = document.readUnsigned("sourceHttpBroker", "listenerPort", 0U, 65535U);
    if (!listenerPortResult.second.empty()) {
        const std::string rawValue = document.lastValue("sourceHttpBroker", "listenerPort").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "sourceHttpBroker",
            "listenerPort",
            rawValue,
            std::to_string(parsed.listenerPort),
            listenerPortResult.second);
    }
    if (listenerPortResult.first.has_value()) {
        parsed.listenerPort = static_cast<std::uint16_t>(*listenerPortResult.first);
    }

    const auto keepAliveResult = document.readUnsigned(
        "sourceHttpBroker",
        "keepAliveSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!keepAliveResult.second.empty()) {
        const std::string rawValue = document.lastValue("sourceHttpBroker", "keepAliveSeconds").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "sourceHttpBroker",
            "keepAliveSeconds",
            rawValue,
            std::to_string(parsed.keepAliveSeconds),
            keepAliveResult.second);
    }
    if (keepAliveResult.first.has_value()) {
        parsed.keepAliveSeconds = static_cast<std::uint32_t>(*keepAliveResult.first);
    }

    const auto cleanResult = document.readBool("sourceHttpBroker", "clean");
    if (!cleanResult.second.empty()) {
        const std::string rawValue = document.lastValue("sourceHttpBroker", "clean").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "sourceHttpBroker",
            "clean",
            rawValue,
            parsed.clean ? "true" : "false",
            cleanResult.second);
    }
    if (cleanResult.first.has_value()) {
        parsed.clean = *cleanResult.first;
    }

    SubscriptionMap parsedSubscriptions{};
    const auto structuredSubscriptionsResult = tryLoadStructuredSourceSubscriptionsFromIni(document);
    if (!structuredSubscriptionsResult.subscriptions.has_value()) {
        logConfigFallbackWarning(
            "broker_connector_client",
            "subscription",
            "*",
            "<composite>",
            "#=1",
            structuredSubscriptionsResult.errorMessage);
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

    const auto receiverPortResult = document.readUnsigned("receiverMqttBroker", "port", 1U, 65535U);
    if (!receiverPortResult.second.empty()) {
        const std::string rawValue = document.lastValue("receiverMqttBroker", "port").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "receiverMqttBroker",
            "port",
            rawValue,
            std::to_string(parsed.brokerPort),
            receiverPortResult.second);
    }
    if (receiverPortResult.first.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*receiverPortResult.first);
    }

    const auto reconnectDelayResult = document.readUnsigned(
        "receiverMqttBroker",
        "reconnectDelayMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!reconnectDelayResult.second.empty()) {
        const std::string rawValue = document.lastValue("receiverMqttBroker", "reconnectDelayMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "receiverMqttBroker",
            "reconnectDelayMs",
            rawValue,
            std::to_string(parsed.reconnectDelay.count()),
            reconnectDelayResult.second);
    }
    if (reconnectDelayResult.first.has_value()) {
        parsed.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult.first};
    }

    const auto keepAliveSecondsResult = document.readUnsigned(
        "receiverMqttBroker",
        "keepAliveSeconds",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!keepAliveSecondsResult.second.empty()) {
        const std::string rawValue = document.lastValue("receiverMqttBroker", "keepAliveSeconds").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "receiverMqttBroker",
            "keepAliveSeconds",
            rawValue,
            std::to_string(parsed.keepAliveInterval.count() / static_cast<long long>(k_milliseconds_per_second)),
            keepAliveSecondsResult.second);
    }
    if (keepAliveSecondsResult.first.has_value()) {
        parsed.keepAliveInterval = std::chrono::milliseconds{
            *keepAliveSecondsResult.first * k_milliseconds_per_second};
    }

    const auto loopSleepResult = document.readUnsigned(
        "receiverMqttBroker",
        "loopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!loopSleepResult.second.empty()) {
        const std::string rawValue = document.lastValue("receiverMqttBroker", "loopSleepMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "receiverMqttBroker",
            "loopSleepMs",
            rawValue,
            std::to_string(parsed.loopSleep.count()),
            loopSleepResult.second);
    }
    if (loopSleepResult.first.has_value()) {
        parsed.loopSleep = std::chrono::milliseconds{*loopSleepResult.first};
    }

    const auto lifecycleTraceResult = document.readBool("receiverMqttBroker", "enableLifecycleTrace");
    if (!lifecycleTraceResult.second.empty()) {
        const std::string rawValue =
            document.lastValue("receiverMqttBroker", "enableLifecycleTrace").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "receiverMqttBroker",
            "enableLifecycleTrace",
            rawValue,
            parsed.enableLifecycleTrace ? "true" : "false",
            lifecycleTraceResult.second);
    }
    if (lifecycleTraceResult.first.has_value()) {
        parsed.enableLifecycleTrace = *lifecycleTraceResult.first;
    }

    return {.config = std::move(parsed), .errorMessage = ""};
}

void applyMonitoringAndMessageLogConfig(
    const IniDocument& document,
    BrokerConnectorClientRuntimeConfig& parsed) {
    const auto sourceTraceResult = document.readBool("monitoring", "sourceLifecycleTrace");
    if (!sourceTraceResult.second.empty()) {
        const std::string rawValue = document.lastValue("monitoring", "sourceLifecycleTrace").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "monitoring",
            "sourceLifecycleTrace",
            rawValue,
            parsed.sourceLifecycleConfig.enableTrace ? "true" : "false",
            sourceTraceResult.second);
    }
    if (sourceTraceResult.first.has_value()) {
        parsed.sourceLifecycleConfig.enableTrace = *sourceTraceResult.first;
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
        logConfigFallbackWarning(
            "broker_connector_client",
            "monitoring",
            "log*",
            "<composite>",
            "defaults",
            messageLogConfigError);
    }

    parsed.sourceConfig.logIncomingMessages = messageLogConfig.enableIncoming;
    parsed.receiverConfig.enableMessageTrace = messageLogConfig.enableOutgoing;
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
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!sourceReconnectResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "reconnectDelayMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "reconnectDelayMs",
            rawValue,
            std::to_string(parsed.sourceLifecycleConfig.reconnectDelay.count()),
            sourceReconnectResult.second);
    }
    if (sourceReconnectResult.first.has_value()) {
        parsed.sourceLifecycleConfig.reconnectDelay = std::chrono::milliseconds{*sourceReconnectResult.first};
    }

    const auto sourceLoopSleepResult = document.readUnsigned(
        "automation",
        "sourceLoopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!sourceLoopSleepResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "sourceLoopSleepMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "sourceLoopSleepMs",
            rawValue,
            std::to_string(parsed.sourceLifecycleConfig.loopSleep.count()),
            sourceLoopSleepResult.second);
    }
    if (sourceLoopSleepResult.first.has_value()) {
        parsed.sourceLifecycleConfig.loopSleep = std::chrono::milliseconds{*sourceLoopSleepResult.first};
    }

    const auto sourceKeepAliveResult = document.readUnsigned(
        "automation",
        "sourceKeepAliveIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!sourceKeepAliveResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "sourceKeepAliveIntervalMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "sourceKeepAliveIntervalMs",
            rawValue,
            std::to_string(parsed.sourceLifecycleConfig.keepAliveInterval.count()),
            sourceKeepAliveResult.second);
    }
    if (sourceKeepAliveResult.first.has_value()) {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{*sourceKeepAliveResult.first};
    } else {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{
            static_cast<std::uint64_t>(parsed.sourceConfig.keepAliveSeconds) * k_milliseconds_per_second};
    }

    const auto maxRetryResult = document.readUnsigned(
        "automation",
        "maxPublishRetries",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!maxRetryResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "maxPublishRetries").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "maxPublishRetries",
            rawValue,
            std::to_string(parsed.relayPolicyConfig.maxPublishRetries),
            maxRetryResult.second);
    }
    if (maxRetryResult.first.has_value()) {
        parsed.relayPolicyConfig.maxPublishRetries = static_cast<std::uint32_t>(*maxRetryResult.first);
    }

    const auto backoffResult = document.readUnsigned(
        "automation",
        "publishRetryBackoffMs",
        0U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!backoffResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "publishRetryBackoffMs").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "publishRetryBackoffMs",
            rawValue,
            std::to_string(parsed.relayPolicyConfig.publishRetryBackoff.count()),
            backoffResult.second);
    }
    if (backoffResult.first.has_value()) {
        parsed.relayPolicyConfig.publishRetryBackoff = std::chrono::milliseconds{*backoffResult.first};
    }

    const auto normalizeQosResult = document.readBool("automation", "normalizeQosToAtLeastOnce");
    if (!normalizeQosResult.second.empty()) {
        const std::string rawValue =
            document.lastValue("automation", "normalizeQosToAtLeastOnce").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "normalizeQosToAtLeastOnce",
            rawValue,
            parsed.relayPolicyConfig.normalizeQosToAtLeastOnce ? "true" : "false",
            normalizeQosResult.second);
    }
    if (normalizeQosResult.first.has_value()) {
        parsed.relayPolicyConfig.normalizeQosToAtLeastOnce = *normalizeQosResult.first;
    }

    const auto retainResult = document.readBool("automation", "retainPassthrough");
    if (!retainResult.second.empty()) {
        const std::string rawValue = document.lastValue("automation", "retainPassthrough").value_or("<missing>");
        logConfigFallbackWarning(
            "broker_connector_client",
            "automation",
            "retainPassthrough",
            rawValue,
            parsed.relayPolicyConfig.retainPassthrough ? "true" : "false",
            retainResult.second);
    }
    if (retainResult.first.has_value()) {
        parsed.relayPolicyConfig.retainPassthrough = *retainResult.first;
    }

    applyMonitoringAndMessageLogConfig(document, parsed);

    return {.config = std::move(parsed), .errorMessage = ""};
}

} // namespace yaha
