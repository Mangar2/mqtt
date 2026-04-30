#include "yaha/mqtt_client/mqtt_client_config.h"

#include "yaha/ini/ini_value_reader.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace yaha {

bool tryLoadMqttClientConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage) {
    if (const auto host = iniLookupLastValue(document, "mqtt", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    std::uint64_t parsed = 0U;
    if (!iniTryReadUnsigned(document,
                            "mqtt",
                            "port",
                            1U,
                            65535U,
                            parsed,
                            "mqtt.port",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "mqtt", "port").has_value()) {
        output.brokerPort = static_cast<std::uint16_t>(parsed);
    }

    if (const auto clientId = iniLookupLastValue(document, "mqtt", "clientId");
        clientId.has_value()) {
        output.clientId = *clientId;
    }

    if (!iniTryReadUnsigned(document,
                            "mqtt",
                            "reconnectDelayMs",
                            1U,
                            600000U,
                            parsed,
                            "mqtt.reconnectDelayMs",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "mqtt", "reconnectDelayMs").has_value()) {
        output.reconnectDelay = std::chrono::milliseconds{parsed};
    }

    if (!iniTryReadUnsigned(document,
                            "mqtt",
                            "keepAliveIntervalMs",
                            1U,
                            600000U,
                            parsed,
                            "mqtt.keepAliveIntervalMs",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "mqtt", "keepAliveIntervalMs").has_value()) {
        output.keepAliveInterval = std::chrono::milliseconds{parsed};
    }

    if (!iniTryReadUnsigned(document,
                            "mqtt",
                            "loopSleepMs",
                            1U,
                            1000U,
                            parsed,
                            "mqtt.loopSleepMs",
                            errorMessage)) {
        return false;
    }
    if (iniLookupLastValue(document, "mqtt", "loopSleepMs").has_value()) {
        output.loopSleep = std::chrono::milliseconds{parsed};
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
        std::uint64_t qosValue = 0U;
        if (!iniTryParseUnsigned(entry.value, 0U, 2U, qosValue)) {
            errorMessage = "invalid qos for subscription '" + entry.key + "'";
            return false;
        }

        parsed[entry.key] = static_cast<Qos>(qosValue);
    }

    output = std::move(parsed);
    return true;
}

} // namespace yaha
