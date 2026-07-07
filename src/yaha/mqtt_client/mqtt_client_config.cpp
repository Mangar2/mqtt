#include "yaha/mqtt_client/mqtt_client_config.h"

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace yaha {

bool tryLoadMqttClientConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto host = document.lastValue("mqtt", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    const auto portResult = document.readUnsigned("mqtt", "port", 1U, 65535U, std::to_string(output.brokerPort));
    if (portResult.has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(*portResult);
    }

    if (const auto clientId = document.lastValue("mqtt", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    const auto reconnectDelayResult = document.readUnsigned(
        "mqtt",
        "reconnectDelayMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.reconnectDelay.count()));
    if (reconnectDelayResult.has_value()) {
        output.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult};
    }

    const auto keepAliveResult = document.readUnsigned(
        "mqtt",
        "keepAliveIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.keepAliveInterval.count()));
    if (keepAliveResult.has_value()) {
        output.keepAliveInterval = std::chrono::milliseconds{*keepAliveResult};
    }

    const auto loopSleepResult = document.readUnsigned(
        "mqtt",
        "loopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::to_string(output.loopSleep.count()));
    if (loopSleepResult.has_value()) {
        output.loopSleep = std::chrono::milliseconds{*loopSleepResult};
    }

    const auto logReasonResult = document.readBool("mqtt", "logReason", output.logReason);
    if (logReasonResult.has_value()) {
        output.logReason = *logReasonResult;
    }

    errorMessage.clear();
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
