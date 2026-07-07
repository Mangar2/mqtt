#include "yaha/opensensemap/opensensemap_component.h"

#include "json/json_value.h"

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

void logError(const std::string& reasonText, const Message& message) {
    std::cerr << "opensensemap[error]"
              << " topic=" << message.topic()
              << " reason=" << reasonText
              << '\n' << std::flush;
}

void logHttpError(const int statusCode, const std::string& reasonText, const Message& message) {
    std::cerr << "opensensemap[error]"
              << " topic=" << message.topic()
              << " httpStatus=" << statusCode
              << " reason=" << reasonText
              << '\n' << std::flush;
}

void logUploadSuppressed(const std::string& topicName,
                         const std::string& sensorIdentifier,
                         const std::uint64_t elapsedSeconds,
                         const std::uint32_t minUploadIntervalSeconds) {
    std::cerr << "opensensemap[warn]"
              << " topic=" << topicName
              << " sensorId=" << sensorIdentifier
              << " reason=upload interval guard active"
              << " elapsedSeconds=" << elapsedSeconds
              << " minUploadIntervalSeconds=" << minUploadIntervalSeconds
              << " action=ignore"
              << '\n' << std::flush;
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
        const std::string reasonText =
            std::format("topic {} not found in opensensemap sensor configuration", message.topic());
        logError(reasonText, message);
        const Message statusMessage = buildStatusMessage(
            kHttpStatusNotFound,
            message,
            reasonText);
        publishStatusMessage(statusMessage);
        return;
    }

    const auto maybeNumericValue = toNumericValue(message.value());
    if (!maybeNumericValue.has_value()) {
        const std::string reasonText =
            std::format("topic {} contains non-numeric value {}", message.topic(), valueToText(message.value()));
        logError(reasonText, message);
        const Message statusMessage = buildStatusMessage(
            kHttpStatusUnprocessableEntity,
            message,
            reasonText);
        publishStatusMessage(statusMessage);
        return;
    }

    if (!requestSender_) {
        constexpr const char* kReasonText = "opensensemap request sender callback is missing";
        logError(kReasonText, message);
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            kReasonText);
        publishStatusMessage(statusMessage);
        return;
    }

    std::uint64_t elapsedSeconds = 0U;
    if (shouldIgnoreBecauseUploadTooFrequent(*sensorConfig, elapsedSeconds)) {
        logUploadSuppressed(
            message.topic(),
            sensorConfig->sensorIdentifier,
            elapsedSeconds,
            sensorConfig->minUploadIntervalSeconds);
        return;
    }

    const std::string requestPath = makeRequestPath(config_.boxIdentifier, sensorConfig->sensorIdentifier);
    const std::string requestPayload = makeRequestPayload(*maybeNumericValue);

    try {
        const OpenSenseMapHttpResult result = requestSender_(requestPath, requestPayload);
        const std::string resultReason = buildResultReason(message.topic(), *maybeNumericValue, result);
        if (result.statusCode != kHttpStatusCreated) {
            logHttpError(result.statusCode, resultReason, message);
        }
        const Message statusMessage = buildStatusMessage(result.statusCode, message, resultReason);
        publishStatusMessage(statusMessage);
    } catch (const std::exception& exceptionValue) {
        const std::string reasonText = std::format("opensensemap request failed: {}", exceptionValue.what());
        logError(reasonText, message);
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            reasonText);
        publishStatusMessage(statusMessage);
    } catch (...) {
        constexpr const char* kReasonText = "opensensemap request failed: unknown";
        logError(kReasonText, message);
        const Message statusMessage = buildStatusMessage(
            kHttpStatusInternalServerError,
            message,
            kReasonText);
        publishStatusMessage(statusMessage);
    }
}

void OpenSenseMapComponent::run() {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    running_ = true;
    lastUploadBySensorId_.clear();
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
    mqtt::json::JsonValue payload = mqtt::json::JsonValue::object();
    payload["value"] = mqtt::json::JsonValue{numericValue};
    return payload.stringify();
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
    const auto parsedJson = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedJson.has_value() || !parsedJson->is_object()) {
        return payloadText;
    }

    if (!parsedJson->contains("message")) {
        return payloadText;
    }

    const mqtt::json::JsonValue& messageValue = parsedJson->at("message");
    if (!messageValue.is_string()) {
        return payloadText;
    }

    return messageValue.as_string();
}

Message OpenSenseMapComponent::buildStatusMessage(
    const int statusCode,
    const Message& sourceMessage,
    const std::string& resultReason) {
    const std::string targetTopic = (statusCode == kHttpStatusCreated)
        ? "$MONITOR/opensensemap/success"
        : "$MONITOR/opensensemap/error";

    Message statusMessage{targetTopic, static_cast<double>(statusCode), Qos::AtLeastOnce, false};
    for (const auto& reasonEntry : sourceMessage.reason()) {
        statusMessage.addReason(reasonEntry.message);
    }
    statusMessage.addReason(resultReason);
    return statusMessage;
}

bool OpenSenseMapComponent::shouldIgnoreBecauseUploadTooFrequent(
    const OpenSenseMapSensorConfig& sensorConfig,
    std::uint64_t& elapsedSecondsOut) {
    elapsedSecondsOut = 0U;
    if (sensorConfig.minUploadIntervalSeconds == 0U) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> stateLock{stateMutex_};

    const auto iterator = lastUploadBySensorId_.find(sensorConfig.sensorIdentifier);
    if (iterator == lastUploadBySensorId_.end()) {
        lastUploadBySensorId_[sensorConfig.sensorIdentifier] = now;
        return false;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - iterator->second);
    elapsedSecondsOut = static_cast<std::uint64_t>(elapsed.count());
    if (elapsedSecondsOut < sensorConfig.minUploadIntervalSeconds) {
        return true;
    }

    iterator->second = now;
    return false;
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
