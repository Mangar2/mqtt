#include "yaha/pushover_client/pushover_client_app.h"

#include "yaha/mqtt_client/mqtt_client_config.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yaha {

namespace {

constexpr std::string_view kPushoverSection{"pushover"};
constexpr std::string_view kDeviceSection{"device"};
constexpr std::string_view kSubscriptionSection{"subscription"};
constexpr std::size_t kCommandReadBufferSize{256U};

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

[[nodiscard]] std::string shellQuote(const std::string& rawText) {
    std::string quotedText{"'"};
    for (const char currentChar : rawText) {
        if (currentChar == '\'') {
            quotedText += "'\\''";
        } else {
            quotedText.push_back(currentChar);
        }
    }
    quotedText.push_back('\'');
    return quotedText;
}

[[nodiscard]] std::pair<int, std::string> executeCommand(const std::string& commandText) {
    FILE* processHandle = popen(commandText.c_str(), "r");
    if (processHandle == nullptr) {
        throw std::runtime_error("failed to start curl process");
    }

    std::string outputText{};
    std::array<char, kCommandReadBufferSize> readBuffer{};
    while (fgets(readBuffer.data(), static_cast<int>(readBuffer.size()), processHandle) != nullptr) {
        outputText += readBuffer.data();
    }

    const int exitStatus = pclose(processHandle);
    return {exitStatus, std::move(outputText)};
}

[[nodiscard]] PushoverHttpResult parseCurlOutput(const std::string& outputText) {
    const std::string statusMarker{"\n__YAHA_STATUS__:"};
    const std::string contentTypeMarker{"\n__YAHA_CTYPE__:"};

    const std::size_t statusPosition = outputText.rfind(statusMarker);
    const std::size_t contentTypePosition = outputText.rfind(contentTypeMarker);
    if (statusPosition == std::string::npos || contentTypePosition == std::string::npos
        || contentTypePosition <= statusPosition) {
        throw std::runtime_error("failed to parse curl response metadata");
    }

    const std::string bodyText = outputText.substr(0U, statusPosition);
    const std::size_t statusValueStart = statusPosition + statusMarker.size();
    const std::size_t statusValueEnd = contentTypePosition;
    const std::string statusText = outputText.substr(statusValueStart, statusValueEnd - statusValueStart);

    int statusCode = 0;
    try {
        statusCode = std::stoi(statusText);
    } catch (...) {
        throw std::runtime_error("failed to parse HTTP status code from curl output");
    }

    std::string contentType = outputText.substr(contentTypePosition + contentTypeMarker.size());
    while (!contentType.empty() && (contentType.back() == '\n' || contentType.back() == '\r')) {
        contentType.pop_back();
    }

    return PushoverHttpResult{
        .statusCode = statusCode,
        .payload = bodyText,
        .contentType = contentType,
    };
}

[[nodiscard]] bool tryLoadDevicesFromIni(
    const IniDocument& document,
    std::vector<std::string>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection(kDeviceSection);
    if (section == nullptr || section->entries().empty()) {
        errorMessage = "missing [device] entries";
        return false;
    }

    std::vector<std::string> parsedDevices{};
    for (const auto& entry : section->entries()) {
        if (entry.key != "name") {
            errorMessage = "invalid key in [device] (expected 'name', got '" + entry.key + "')";
            return false;
        }

        if (entry.value.empty()) {
            errorMessage = "device.name must not be empty";
            return false;
        }

        parsedDevices.push_back(entry.value);
    }

    output = std::move(parsedDevices);
    return true;
}

struct PendingSubscription {
    std::optional<std::string> topic{};
    std::optional<Qos> qos{};
};

[[nodiscard]] bool flushPendingSubscription(
    PendingSubscription& pending,
    std::vector<PushoverSubscriptionConfig>& parsed,
    std::string& errorMessage) {
    if (!pending.topic.has_value() && !pending.qos.has_value()) {
        return true;
    }

    if (!pending.topic.has_value() || !pending.qos.has_value()) {
        errorMessage = "incomplete [subscription] entry (expected topic and qos)";
        return false;
    }

    parsed.push_back(PushoverSubscriptionConfig{.topicFilter = *pending.topic, .qos = *pending.qos});
    pending.topic.reset();
    pending.qos.reset();
    return true;
}

[[nodiscard]] bool applySubscriptionTopic(
    const IniDocument::Entry& entry,
    PendingSubscription& pending,
    std::vector<PushoverSubscriptionConfig>& parsed,
    std::string& errorMessage) {
    if (!flushPendingSubscription(pending, parsed, errorMessage)) {
        return false;
    }

    if (entry.value.empty()) {
        errorMessage = "subscription.topic must not be empty";
        return false;
    }

    pending.topic = entry.value;
    return true;
}

[[nodiscard]] bool applySubscriptionQos(
    const IniDocument::Entry& entry,
    PendingSubscription& pending,
    std::string& errorMessage) {
    if (!pending.topic.has_value()) {
        errorMessage = "subscription.qos requires subscription.topic first";
        return false;
    }
    if (pending.qos.has_value()) {
        errorMessage = "duplicate subscription.qos in one [subscription] entry";
        return false;
    }

    const auto qosValue = IniDocument::parseUnsigned(entry.value, 0U, 2U);
    if (!qosValue.has_value()) {
        errorMessage = "invalid qos for subscription topic '" + *pending.topic + "'";
        return false;
    }

    pending.qos = static_cast<Qos>(*qosValue);
    return true;
}

[[nodiscard]] bool applySubscriptionEntry(
    const IniDocument::Entry& entry,
    PendingSubscription& pending,
    std::vector<PushoverSubscriptionConfig>& parsed,
    std::string& errorMessage) {
    if (entry.key == "topic") {
        return applySubscriptionTopic(entry, pending, parsed, errorMessage);
    }

    if (entry.key == "qos") {
        return applySubscriptionQos(entry, pending, errorMessage);
    }

    errorMessage = "invalid key in [subscription] (expected topic or qos, got '" + entry.key + "')";
    return false;
}

[[nodiscard]] bool tryLoadSubscriptionsFromIni(
    const IniDocument& document,
    std::vector<PushoverSubscriptionConfig>& output,
    std::string& errorMessage) {
    const IniDocument::Section* section = document.findSection(kSubscriptionSection);
    if (section == nullptr || section->entries().empty()) {
        errorMessage = "missing [subscription] entries";
        return false;
    }

    std::vector<PushoverSubscriptionConfig> parsed{};
    PendingSubscription pending{};

    for (const auto& entry : section->entries()) {
        if (!applySubscriptionEntry(entry, pending, parsed, errorMessage)) {
            return false;
        }
    }

    if (!flushPendingSubscription(pending, parsed, errorMessage)) {
        return false;
    }

    if (parsed.empty()) {
        errorMessage = "missing [subscription] entries";
        return false;
    }

    output = std::move(parsed);
    return true;
}

} // namespace

bool tryLoadPushoverConfigFromIni(
    const IniDocument& document,
    PushoverConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    if (const auto host = document.lastValue(kPushoverSection, "host"); host.has_value()) {
        output.host = *host;
    }

    if (const auto path = document.lastValue(kPushoverSection, "path"); path.has_value()) {
        output.path = *path;
    }

    if (const auto token = document.lastValue(kPushoverSection, "token"); token.has_value()) {
        output.token = *token;
    }

    if (const auto user = document.lastValue(kPushoverSection, "user"); user.has_value()) {
        output.user = *user;
    }

    const auto portResult = document.readUnsigned(kPushoverSection, "port", 1U, 65535U);
    if (!portResult.second.empty()) {
        const std::string rawValue = document.lastValue(kPushoverSection, "port").value_or("<missing>");
        logConfigFallbackWarning(
            "pushover_client",
            kPushoverSection,
            "port",
            rawValue,
            std::to_string(output.port),
            portResult.second);
    }
    if (portResult.first.has_value()) {
        output.port = static_cast<std::uint16_t>(*portResult.first);
    }

    if (output.host.empty()) {
        errorMessage = "pushover.host must not be empty";
        return false;
    }

    if (output.path.empty()) {
        errorMessage = "pushover.path must not be empty";
        return false;
    }

    if (output.token.empty()) {
        errorMessage = "pushover.token must not be empty";
        return false;
    }

    if (output.user.empty()) {
        errorMessage = "pushover.user must not be empty";
        return false;
    }

    std::vector<std::string> parsedDevices{};
    if (!tryLoadDevicesFromIni(document, parsedDevices, errorMessage)) {
        return false;
    }

    std::vector<PushoverSubscriptionConfig> parsedSubscriptions{};
    if (!tryLoadSubscriptionsFromIni(document, parsedSubscriptions, errorMessage)) {
        return false;
    }

    output.devices = std::move(parsedDevices);
    output.subscriptions = std::move(parsedSubscriptions);
    return true;
}

bool tryLoadPushoverClientRuntimeConfigFromIni(
    const IniDocument& document,
    PushoverClientRuntimeConfig& output,
    std::string& errorMessage) {
    errorMessage.clear();

    PushoverClientRuntimeConfig parsed{};
    if (!tryLoadPushoverConfigFromIni(document, parsed.pushoverConfig, errorMessage)) {
        return false;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(document, parsed.mqttConfig, mqttErrorMessage)) {
        logConfigFallbackWarning(
            "pushover_client",
            "mqtt",
            "*",
            "<composite>",
            "defaults",
            mqttErrorMessage);
    }

    output = std::move(parsed);
    return true;
}

PushoverRequestSender makePushoverRequestSender(const PushoverConfig& config) {
    return [host = config.host,
            path = config.path,
            port = config.port](
               const std::string&,
               const std::string& requestPayload) {
        const std::string targetUrl =
            "https://" + host + ":" + std::to_string(port) + path;

        const std::string commandText =
            "curl --silent --show-error --max-time 5 --request POST"
            " --header " + shellQuote("content-type: application/json; charset=UTF-8") +
            " --data " + shellQuote(requestPayload) +
            " --write-out " +
            shellQuote("\n__YAHA_STATUS__:%{http_code}\n__YAHA_CTYPE__:%{content_type}") +
            " " + shellQuote(targetUrl);

        const auto [exitStatus, outputText] = executeCommand(commandText);
        if (exitStatus != 0) {
            throw std::runtime_error("curl request execution failed");
        }

        return parseCurlOutput(outputText);
    };
}

} // namespace yaha
