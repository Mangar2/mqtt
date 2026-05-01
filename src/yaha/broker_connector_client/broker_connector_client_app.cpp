#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace yaha {

bool tryLoadSourceHttpBrokerConfigFromIni(
    const IniDocument& document,
    SourceHttpBrokerConfig& output,
    std::string& errorMessage) {
    if (const auto host = document.lastValue("sourceHttpBroker", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    if (const auto clientId = document.lastValue("sourceHttpBroker", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    if (const auto listenerHost = document.lastValue("sourceHttpBroker", "listenerHost");
        listenerHost.has_value()) {
        output.listenerHost = *listenerHost;
    }

    const auto sourcePortResult = document.readUnsigned("sourceHttpBroker", "port", 1U, 65535U);
    if (!sourcePortResult.second.empty()) {
        errorMessage = sourcePortResult.second;
        return false;
    }
    if (sourcePortResult.first.has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(*sourcePortResult.first);
    }

    const auto listenerPortResult = document.readUnsigned("sourceHttpBroker", "listenerPort", 0U, 65535U);
    if (!listenerPortResult.second.empty()) {
        errorMessage = listenerPortResult.second;
        return false;
    }
    if (listenerPortResult.first.has_value()) {
        output.listenerPort = static_cast<std::uint16_t>(*listenerPortResult.first);
    }

    const auto keepAliveResult = document.readUnsigned("sourceHttpBroker", "keepAliveSeconds", 1U, 86400U);
    if (!keepAliveResult.second.empty()) {
        errorMessage = keepAliveResult.second;
        return false;
    }
    if (keepAliveResult.first.has_value()) {
        output.keepAliveSeconds = static_cast<std::uint32_t>(*keepAliveResult.first);
    }

    const auto cleanResult = document.readBool("sourceHttpBroker", "clean");
    if (!cleanResult.second.empty()) {
        errorMessage = cleanResult.second;
        return false;
    }
    if (cleanResult.first.has_value()) {
        output.clean = *cleanResult.first;
    }

    SubscriptionMap parsedSubscriptions{};
    if (!tryLoadSubscriptionsFromIni(document, "sourceSubscriptions", parsedSubscriptions, errorMessage)) {
        return false;
    }

    if (parsedSubscriptions.empty()) {
        output.subscribeTopics = {{"#", Qos::AtLeastOnce}};
    } else {
        output.subscribeTopics = std::move(parsedSubscriptions);
    }

    return true;
}

bool tryLoadReceiverMqttBrokerConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage) {
    if (const auto host = document.lastValue("receiverMqttBroker", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    if (const auto clientId = document.lastValue("receiverMqttBroker", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    const auto receiverPortResult = document.readUnsigned("receiverMqttBroker", "port", 1U, 65535U);
    if (!receiverPortResult.second.empty()) {
        errorMessage = receiverPortResult.second;
        return false;
    }
    if (receiverPortResult.first.has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(*receiverPortResult.first);
    }

    const auto reconnectDelayResult = document.readUnsigned("receiverMqttBroker", "reconnectDelayMs", 1U, 600000U);
    if (!reconnectDelayResult.second.empty()) {
        errorMessage = reconnectDelayResult.second;
        return false;
    }
    if (reconnectDelayResult.first.has_value()) {
        output.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult.first};
    }

    const auto keepAliveSecondsResult = document.readUnsigned("receiverMqttBroker", "keepAliveSeconds", 1U, 86400U);
    if (!keepAliveSecondsResult.second.empty()) {
        errorMessage = keepAliveSecondsResult.second;
        return false;
    }
    if (keepAliveSecondsResult.first.has_value()) {
        output.keepAliveInterval = std::chrono::milliseconds{*keepAliveSecondsResult.first * 1000U};
    }

    const auto loopSleepResult = document.readUnsigned("receiverMqttBroker", "loopSleepMs", 1U, 1000U);
    if (!loopSleepResult.second.empty()) {
        errorMessage = loopSleepResult.second;
        return false;
    }
    if (loopSleepResult.first.has_value()) {
        output.loopSleep = std::chrono::milliseconds{*loopSleepResult.first};
    }

    const auto lifecycleTraceResult = document.readBool("receiverMqttBroker", "enableLifecycleTrace");
    if (!lifecycleTraceResult.second.empty()) {
        errorMessage = lifecycleTraceResult.second;
        return false;
    }
    if (lifecycleTraceResult.first.has_value()) {
        output.enableLifecycleTrace = *lifecycleTraceResult.first;
    }

    const auto messageTraceResult = document.readBool("receiverMqttBroker", "enableMessageTrace");
    if (!messageTraceResult.second.empty()) {
        errorMessage = messageTraceResult.second;
        return false;
    }
    if (messageTraceResult.first.has_value()) {
        output.enableMessageTrace = *messageTraceResult.first;
    }

    return true;
}

bool tryLoadBrokerConnectorClientRuntimeConfigFromIni(
    const IniDocument& document,
    BrokerConnectorClientRuntimeConfig& output,
    std::string& errorMessage) {
    BrokerConnectorClientRuntimeConfig parsed{};

    if (!tryLoadSourceHttpBrokerConfigFromIni(document, parsed.sourceConfig, errorMessage)) {
        return false;
    }

    if (!tryLoadReceiverMqttBrokerConfigFromIni(document, parsed.receiverConfig, errorMessage)) {
        return false;
    }

    const auto sourceReconnectResult = document.readUnsigned("automation", "reconnectDelayMs", 1U, 600000U);
    if (!sourceReconnectResult.second.empty()) {
        errorMessage = sourceReconnectResult.second;
        return false;
    }
    if (sourceReconnectResult.first.has_value()) {
        parsed.sourceLifecycleConfig.reconnectDelay = std::chrono::milliseconds{*sourceReconnectResult.first};
    }

    const auto sourceLoopSleepResult = document.readUnsigned("automation", "sourceLoopSleepMs", 1U, 1000U);
    if (!sourceLoopSleepResult.second.empty()) {
        errorMessage = sourceLoopSleepResult.second;
        return false;
    }
    if (sourceLoopSleepResult.first.has_value()) {
        parsed.sourceLifecycleConfig.loopSleep = std::chrono::milliseconds{*sourceLoopSleepResult.first};
    }

    const auto sourceKeepAliveResult = document.readUnsigned("automation", "sourceKeepAliveIntervalMs", 1U, 600000U);
    if (!sourceKeepAliveResult.second.empty()) {
        errorMessage = sourceKeepAliveResult.second;
        return false;
    }
    if (sourceKeepAliveResult.first.has_value()) {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{*sourceKeepAliveResult.first};
    } else {
        parsed.sourceLifecycleConfig.keepAliveInterval = std::chrono::milliseconds{
            static_cast<std::uint64_t>(parsed.sourceConfig.keepAliveSeconds) * 1000U};
    }

    const auto maxRetryResult = document.readUnsigned("automation", "maxPublishRetries", 0U, 1000U);
    if (!maxRetryResult.second.empty()) {
        errorMessage = maxRetryResult.second;
        return false;
    }
    if (maxRetryResult.first.has_value()) {
        parsed.relayPolicyConfig.maxPublishRetries = static_cast<std::uint32_t>(*maxRetryResult.first);
    }

    const auto backoffResult = document.readUnsigned("automation", "publishRetryBackoffMs", 0U, 600000U);
    if (!backoffResult.second.empty()) {
        errorMessage = backoffResult.second;
        return false;
    }
    if (backoffResult.first.has_value()) {
        parsed.relayPolicyConfig.publishRetryBackoff = std::chrono::milliseconds{*backoffResult.first};
    }

    const auto normalizeQosResult = document.readBool("automation", "normalizeQosToAtLeastOnce");
    if (!normalizeQosResult.second.empty()) {
        errorMessage = normalizeQosResult.second;
        return false;
    }
    if (normalizeQosResult.first.has_value()) {
        parsed.relayPolicyConfig.normalizeQosToAtLeastOnce = *normalizeQosResult.first;
    }

    const auto retainResult = document.readBool("automation", "retainPassthrough");
    if (!retainResult.second.empty()) {
        errorMessage = retainResult.second;
        return false;
    }
    if (retainResult.first.has_value()) {
        parsed.relayPolicyConfig.retainPassthrough = *retainResult.first;
    }

    const auto sourceTraceResult = document.readBool("monitoring", "sourceLifecycleTrace");
    if (!sourceTraceResult.second.empty()) {
        errorMessage = sourceTraceResult.second;
        return false;
    }
    if (sourceTraceResult.first.has_value()) {
        parsed.sourceLifecycleConfig.enableTrace = *sourceTraceResult.first;
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
