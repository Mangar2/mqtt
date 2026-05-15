#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <cstdint>
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

    const auto sourcePortResult = document.readUnsigned("sourceHttpBroker", "port", 1U, 65535U);
    if (!sourcePortResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = sourcePortResult.second};
    }
    if (sourcePortResult.first.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*sourcePortResult.first);
    }

    const auto listenerPortResult = document.readUnsigned("sourceHttpBroker", "listenerPort", 0U, 65535U);
    if (!listenerPortResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = listenerPortResult.second};
    }
    if (listenerPortResult.first.has_value()) {
        parsed.listenerPort = static_cast<std::uint16_t>(*listenerPortResult.first);
    }

    const auto keepAliveResult = document.readUnsigned("sourceHttpBroker", "keepAliveSeconds", 1U, 86400U);
    if (!keepAliveResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = keepAliveResult.second};
    }
    if (keepAliveResult.first.has_value()) {
        parsed.keepAliveSeconds = static_cast<std::uint32_t>(*keepAliveResult.first);
    }

    const auto cleanResult = document.readBool("sourceHttpBroker", "clean");
    if (!cleanResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = cleanResult.second};
    }
    if (cleanResult.first.has_value()) {
        parsed.clean = *cleanResult.first;
    }

    SubscriptionMap parsedSubscriptions{};
    const auto structuredSubscriptionsResult = tryLoadStructuredSourceSubscriptionsFromIni(document);
    if (!structuredSubscriptionsResult.subscriptions.has_value()) {
        return {.config = std::nullopt, .errorMessage = structuredSubscriptionsResult.errorMessage};
    }
    parsedSubscriptions = *structuredSubscriptionsResult.subscriptions;

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
        .enableMessageTrace = false,
        .logReason = true};

    if (const auto host = document.lastValue("receiverMqttBroker", "host"); host.has_value()) {
        parsed.brokerHost = *host;
    }

    if (const auto clientId = document.lastValue("receiverMqttBroker", "clientId");
        clientId.has_value()) {
        parsed.clientId = *clientId;
    }

    const auto receiverPortResult = document.readUnsigned("receiverMqttBroker", "port", 1U, 65535U);
    if (!receiverPortResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = receiverPortResult.second};
    }
    if (receiverPortResult.first.has_value()) {
        parsed.brokerPort = static_cast<std::uint16_t>(*receiverPortResult.first);
    }

    const auto reconnectDelayResult = document.readUnsigned("receiverMqttBroker", "reconnectDelayMs", 1U, 600000U);
    if (!reconnectDelayResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = reconnectDelayResult.second};
    }
    if (reconnectDelayResult.first.has_value()) {
        parsed.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult.first};
    }

    const auto keepAliveSecondsResult = document.readUnsigned("receiverMqttBroker", "keepAliveSeconds", 1U, 86400U);
    if (!keepAliveSecondsResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = keepAliveSecondsResult.second};
    }
    if (keepAliveSecondsResult.first.has_value()) {
        parsed.keepAliveInterval = std::chrono::milliseconds{
            *keepAliveSecondsResult.first * k_milliseconds_per_second};
    }

    const auto loopSleepResult = document.readUnsigned("receiverMqttBroker", "loopSleepMs", 1U, 1000U);
    if (!loopSleepResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = loopSleepResult.second};
    }
    if (loopSleepResult.first.has_value()) {
        parsed.loopSleep = std::chrono::milliseconds{*loopSleepResult.first};
    }

    const auto lifecycleTraceResult = document.readBool("receiverMqttBroker", "enableLifecycleTrace");
    if (!lifecycleTraceResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = lifecycleTraceResult.second};
    }
    if (lifecycleTraceResult.first.has_value()) {
        parsed.enableLifecycleTrace = *lifecycleTraceResult.first;
    }

    const auto messageTraceResult = document.readBool("receiverMqttBroker", "enableMessageTrace");
    if (!messageTraceResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = messageTraceResult.second};
    }
    if (messageTraceResult.first.has_value()) {
        parsed.enableMessageTrace = *messageTraceResult.first;
    }

    return {.config = std::move(parsed), .errorMessage = ""};
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

    const auto sourceReconnectResult = document.readUnsigned("automation", "reconnectDelayMs", 1U, 600000U);
    if (!sourceReconnectResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = sourceReconnectResult.second};
    }
    if (sourceReconnectResult.first.has_value()) {
        parsed.sourceLifecycleConfig.reconnectDelay = std::chrono::milliseconds{*sourceReconnectResult.first};
    }

    const auto sourceLoopSleepResult = document.readUnsigned("automation", "sourceLoopSleepMs", 1U, 1000U);
    if (!sourceLoopSleepResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = sourceLoopSleepResult.second};
    }
    if (sourceLoopSleepResult.first.has_value()) {
        parsed.sourceLifecycleConfig.loopSleep = std::chrono::milliseconds{*sourceLoopSleepResult.first};
    }

    const auto sourceKeepAliveResult = document.readUnsigned("automation", "sourceKeepAliveIntervalMs", 1U, 600000U);
    if (!sourceKeepAliveResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = sourceKeepAliveResult.second};
    }
    if (sourceKeepAliveResult.first.has_value()) {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{*sourceKeepAliveResult.first};
    } else {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{
            static_cast<std::uint64_t>(parsed.sourceConfig.keepAliveSeconds) * k_milliseconds_per_second};
    }

    const auto maxRetryResult = document.readUnsigned("automation", "maxPublishRetries", 0U, 1000U);
    if (!maxRetryResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = maxRetryResult.second};
    }
    if (maxRetryResult.first.has_value()) {
        parsed.relayPolicyConfig.maxPublishRetries = static_cast<std::uint32_t>(*maxRetryResult.first);
    }

    const auto backoffResult = document.readUnsigned("automation", "publishRetryBackoffMs", 0U, 600000U);
    if (!backoffResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = backoffResult.second};
    }
    if (backoffResult.first.has_value()) {
        parsed.relayPolicyConfig.publishRetryBackoff = std::chrono::milliseconds{*backoffResult.first};
    }

    const auto normalizeQosResult = document.readBool("automation", "normalizeQosToAtLeastOnce");
    if (!normalizeQosResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = normalizeQosResult.second};
    }
    if (normalizeQosResult.first.has_value()) {
        parsed.relayPolicyConfig.normalizeQosToAtLeastOnce = *normalizeQosResult.first;
    }

    const auto retainResult = document.readBool("automation", "retainPassthrough");
    if (!retainResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = retainResult.second};
    }
    if (retainResult.first.has_value()) {
        parsed.relayPolicyConfig.retainPassthrough = *retainResult.first;
    }

    const auto sourceTraceResult = document.readBool("monitoring", "sourceLifecycleTrace");
    if (!sourceTraceResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = sourceTraceResult.second};
    }
    if (sourceTraceResult.first.has_value()) {
        parsed.sourceLifecycleConfig.enableTrace = *sourceTraceResult.first;
    }

    const auto logReasonResult = document.readBool("monitoring", "logReason");
    if (!logReasonResult.second.empty()) {
        return {.config = std::nullopt, .errorMessage = logReasonResult.second};
    }
    if (logReasonResult.first.has_value()) {
        parsed.sourceConfig.logReason = *logReasonResult.first;
        parsed.receiverConfig.logReason = *logReasonResult.first;
    }

    return {.config = std::move(parsed), .errorMessage = ""};
}

} // namespace yaha
