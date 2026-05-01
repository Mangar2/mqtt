#include "yaha/mqtt_client/mqtt_client_config.h"

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace yaha {

bool tryLoadMqttClientConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage) {
    if (const auto host = document.lastValue("mqtt", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    const auto portResult = document.readUnsigned("mqtt", "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        errorMessage = portResult.second;
        return false;
    }
    if (portResult.first.has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(*portResult.first);
    }

    if (const auto clientId = document.lastValue("mqtt", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    const auto reconnectDelayResult = document.readUnsigned("mqtt", "reconnectDelayMs", 1U, 600000U);
    if (!reconnectDelayResult.second.empty()) {
        errorMessage = reconnectDelayResult.second;
        return false;
    }
    if (reconnectDelayResult.first.has_value()) {
        output.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult.first};
    }

    const auto keepAliveResult = document.readUnsigned("mqtt", "keepAliveIntervalMs", 1U, 600000U);
    if (!keepAliveResult.second.empty()) {
        errorMessage = keepAliveResult.second;
        return false;
    }
    if (keepAliveResult.first.has_value()) {
        output.keepAliveInterval = std::chrono::milliseconds{*keepAliveResult.first};
    }

    const auto loopSleepResult = document.readUnsigned("mqtt", "loopSleepMs", 1U, 1000U);
    if (!loopSleepResult.second.empty()) {
        errorMessage = loopSleepResult.second;
        return false;
    }
    if (loopSleepResult.first.has_value()) {
        output.loopSleep = std::chrono::milliseconds{*loopSleepResult.first};
    }

    return true;
}

bool tryLoadSubscriptionsFromIni(
    const IniDocument& document,
    const std::string_view sectionName,
    SubscriptionMap& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection(sectionName);
    if (section == nullptr || section->entries().empty()) {
        return true;
    }

    SubscriptionMap parsed{};
    for (const auto& entry : section->entries()) {
        const auto qosValue = IniDocument::parseUnsigned(entry.value, 0U, 2U);
        if (!qosValue.has_value()) {
            errorMessage = "invalid qos for subscription '" + entry.key + "'";
            return false;
        }

        parsed[entry.key] = static_cast<Qos>(*qosValue);
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
