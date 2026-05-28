#include "yaha/opensensemap/opensensemap_component.h"

#include <cmath>
#include <cctype>
#include <exception>
#include <format>
#include <iostream>
#include <sstream>
#include <utility>

namespace yaha {

namespace {

constexpr int kHttpStatusCreated{201};
constexpr int kHttpStatusNotFound{404};
constexpr int kHttpStatusUnprocessableEntity{422};
constexpr int kHttpStatusInternalServerError{500};

[[nodiscard]] bool startsWithText(const std::string& textValue, const std::string& prefixText) {
    return textValue.starts_with(prefixText);
}

[[nodiscard]] std::string valueToText(const Value& valueVariant) {
    if (std::holds_alternative<std::string>(valueVariant)) {
        return std::get<std::string>(valueVariant);
    }

    std::ostringstream stream{};
    stream << std::get<double>(valueVariant);
    return stream.str();
}

} // namespace

OpenSenseMapComponent::OpenSenseMapComponent(OpenSenseMapConfig config, OpenSenseMapRequestSender requestSender)
    : config_(std::move(config))
    , requestSender_(std::move(requestSender)) {
}

SubscriptionMap OpenSenseMapComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    for (const auto& sensorConfig : config_.sensors) {
        subscriptions[sensorConfig.topicFilter] = config_.subscribeQos;
    }
    return subscriptions;
}

void OpenSenseMapComponent::handleMessage(const Message& message) {
    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (!running_) {
            return;
        }
    }

    const auto sensorConfig = findSensorForTopic(message.topic());
    if (!sensorConfig.has_value()) {
        const Message statusMessage = buildStatusMessage(
            kHttpStatusNotFound,
            message,
            std::format("topic {} not found in opensensemap sensor configuration", message.topic()));
        publishStatusMessage(statusMessage);
        return;
    }

    const auto maybeNumericValue = toNumericValue(message.value());
    if (!maybeNumericValue.has_value()) {
        const Message statusMessage = buildStatusMessage(
            kHttpStatusUnprocessableEntity,
            message,
            std::format("topic {} contains non-numeric value {}", message.topic(), valueToText(message.value())));
        publishStatusMessage(statusMessage);
        return;
    }

    if (!requestSender_) {
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            "opensensemap request sender callback is missing");
        publishStatusMessage(statusMessage);
        return;
    }

    const std::string requestPath = makeRequestPath(config_.boxIdentifier, sensorConfig->sensorIdentifier);
    const std::string requestPayload = makeRequestPayload(*maybeNumericValue);

    try {
        const OpenSenseMapHttpResult result = requestSender_(requestPath, requestPayload);
        const std::string resultReason = buildResultReason(message.topic(), *maybeNumericValue, result);
        const Message statusMessage = buildStatusMessage(result.statusCode, message, resultReason);
        publishStatusMessage(statusMessage);
    } catch (const std::exception& exceptionValue) {
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            std::format("opensensemap request failed: {}", exceptionValue.what()));
        publishStatusMessage(statusMessage);
    } catch (...) {
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            "opensensemap request failed: unknown");
        publishStatusMessage(statusMessage);
    }
}

void OpenSenseMapComponent::run() {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    running_ = true;
}

void OpenSenseMapComponent::close() {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    running_ = false;
}

void OpenSenseMapComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> publishLock{publishMutex_};
    publishCallback_ = std::move(callback);
}

std::optional<OpenSenseMapSensorConfig> OpenSenseMapComponent::findSensorForTopic(const std::string& topicName) const {
    for (const auto& sensorConfig : config_.sensors) {
        if (sensorConfig.topicFilter == topicName) {
            return sensorConfig;
        }
    }

    return std::nullopt;
}

std::optional<double> OpenSenseMapComponent::toNumericValue(const Value& valueVariant) {
    if (std::holds_alternative<double>(valueVariant)) {
        const double numericValue = std::get<double>(valueVariant);
        if (std::isfinite(numericValue)) {
            return numericValue;
        }
        return std::nullopt;
    }

    const auto& textValue = std::get<std::string>(valueVariant);
    if (textValue.empty()) {
        return std::nullopt;
    }

    std::size_t parsedCharacterCount = 0U;
    double numericValue = 0.0;
    try {
        numericValue = std::stod(textValue, &parsedCharacterCount);
    } catch (...) {
        return std::nullopt;
    }

    if (parsedCharacterCount != textValue.size() || !std::isfinite(numericValue)) {
        return std::nullopt;
    }

    return numericValue;
}

std::string OpenSenseMapComponent::makeRequestPayload(const double numericValue) {
    std::ostringstream stream{};
    stream << R"({"value":)" << numericValue << '}';
    return stream.str();
}

std::string OpenSenseMapComponent::makeRequestPath(
    const std::string& boxIdentifier,
    const std::string& sensorIdentifier) {
    return std::format("/boxes/{}/{}", boxIdentifier, sensorIdentifier);
}

std::string OpenSenseMapComponent::buildResultReason(
    const std::string& topicName,
    const double numericValue,
    const OpenSenseMapHttpResult& result) {
    std::ostringstream valueText{};
    valueText << numericValue;

    if (startsWithText(result.contentType, "application/json")) {
        const std::string jsonMessage = extractJsonMessage(result.payload);
        if (!jsonMessage.empty()) {
            return std::format("{}({}): {}", topicName, valueText.str(), jsonMessage);
        }
    }

    if (!result.payload.empty()) {
        return std::format("{}({}): {}", topicName, valueText.str(), result.payload);
    }

    return std::format("{}({}): status={}", topicName, valueText.str(), result.statusCode);
}

std::string OpenSenseMapComponent::extractJsonMessage(const std::string& payloadText) {
    const std::string keyText{"\"message\""};
    const std::size_t keyPosition = payloadText.find(keyText);
    if (keyPosition == std::string::npos) {
        return payloadText;
    }

    std::size_t parsePosition = keyPosition + keyText.size();
    while (parsePosition < payloadText.size() && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size() || payloadText[parsePosition] != ':') {
        return payloadText;
    }
    parsePosition += 1U;

    while (parsePosition < payloadText.size() && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size() || payloadText[parsePosition] != '"') {
        return payloadText;
    }
    parsePosition += 1U;

    std::string parsedText{};
    while (parsePosition < payloadText.size()) {
        const char currentChar = payloadText[parsePosition++];
        if (currentChar == '"') {
            return parsedText;
        }

        if (currentChar == '\\') {
            if (parsePosition >= payloadText.size()) {
                break;
            }
            parsedText.push_back(payloadText[parsePosition++]);
            continue;
        }

        parsedText.push_back(currentChar);
    }

    return payloadText;
}

Message OpenSenseMapComponent::buildStatusMessage(
    const int statusCode,
    const Message& sourceMessage,
    const std::string& resultReason) {
    const std::string targetTopic = (statusCode == kHttpStatusCreated)
        ? "$SYS/opensensemap/success"
        : "$SYS/opensensemap/error";

    Message statusMessage{targetTopic, static_cast<double>(statusCode), Qos::AtLeastOnce, false};
    for (const auto& reasonEntry : sourceMessage.reason()) {
        statusMessage.addReason(reasonEntry.message);
    }
    statusMessage.addReason(resultReason);
    return statusMessage;
}

void OpenSenseMapComponent::publishStatusMessage(const Message& statusMessage) const {
    std::lock_guard<std::mutex> publishLock{publishMutex_};
    if (!publishCallback_) {
        std::cout << "opensensemap[error] publish_callback_missing"
                  << " topic=" << statusMessage.topic() << '\n' << std::flush;
        return;
    }

    const PublishResult result = publishCallback_(statusMessage);
    if (result.success) {
        return;
    }

    std::cout << "opensensemap[error] status_publish_failed"
              << " topic=" << statusMessage.topic()
              << " reason=" << result.reason
              << '\n' << std::flush;
}

} // namespace yaha
