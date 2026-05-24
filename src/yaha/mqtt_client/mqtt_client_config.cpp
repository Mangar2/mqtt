#include "yaha/mqtt_client/mqtt_client_config.h"

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

void logConfigFallbackWarning(
    const std::string_view sectionName,
    const std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText) {
    std::cerr << "mqtt_client[warn] config_fallback"
              << " section=" << sectionName
              << " key=" << keyName
              << " value='" << rawValue << "'"
              << " default='" << defaultValue << "'"
              << " reason='" << reasonText << "'"
              << '\n' << std::flush;
}

} // namespace

bool tryLoadMqttClientConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto host = document.lastValue("mqtt", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    const auto portResult = document.readUnsigned("mqtt", "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        const std::string rawValue = document.lastValue("mqtt", "port").value_or("<missing>");
        logConfigFallbackWarning("mqtt", "port", rawValue, std::to_string(output.brokerPort), portResult.second);
    }
    if (portResult.first.has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(*portResult.first);
    }

    if (const auto clientId = document.lastValue("mqtt", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    const auto reconnectDelayResult = document.readUnsigned(
        "mqtt",
        "reconnectDelayMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!reconnectDelayResult.second.empty()) {
        const std::string rawValue = document.lastValue("mqtt", "reconnectDelayMs").value_or("<missing>");
        logConfigFallbackWarning(
            "mqtt",
            "reconnectDelayMs",
            rawValue,
            std::to_string(output.reconnectDelay.count()),
            reconnectDelayResult.second);
    }
    if (reconnectDelayResult.first.has_value()) {
        output.reconnectDelay = std::chrono::milliseconds{*reconnectDelayResult.first};
    }

    const auto keepAliveResult = document.readUnsigned(
        "mqtt",
        "keepAliveIntervalMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!keepAliveResult.second.empty()) {
        const std::string rawValue = document.lastValue("mqtt", "keepAliveIntervalMs").value_or("<missing>");
        logConfigFallbackWarning(
            "mqtt",
            "keepAliveIntervalMs",
            rawValue,
            std::to_string(output.keepAliveInterval.count()),
            keepAliveResult.second);
    }
    if (keepAliveResult.first.has_value()) {
        output.keepAliveInterval = std::chrono::milliseconds{*keepAliveResult.first};
    }

    const auto loopSleepResult = document.readUnsigned(
        "mqtt",
        "loopSleepMs",
        1U,
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (!loopSleepResult.second.empty()) {
        const std::string rawValue = document.lastValue("mqtt", "loopSleepMs").value_or("<missing>");
        logConfigFallbackWarning(
            "mqtt",
            "loopSleepMs",
            rawValue,
            std::to_string(output.loopSleep.count()),
            loopSleepResult.second);
    }
    if (loopSleepResult.first.has_value()) {
        output.loopSleep = std::chrono::milliseconds{*loopSleepResult.first};
    }

    const auto logReasonResult = document.readBool("mqtt", "logReason");
    if (!logReasonResult.second.empty()) {
        const std::string rawValue = document.lastValue("mqtt", "logReason").value_or("<missing>");
        logConfigFallbackWarning(
            "mqtt",
            "logReason",
            rawValue,
            output.logReason ? "true" : "false",
            logReasonResult.second);
    }
    if (logReasonResult.first.has_value()) {
        output.logReason = *logReasonResult.first;
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
