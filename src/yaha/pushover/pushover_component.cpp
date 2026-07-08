#include "yaha/pushover/pushover_component.h"

#include "helper/string_helper.h"
#include "json/json_value.h"
#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"

#include <cmath>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace yaha {

namespace {

constexpr int kHttpStatusUnprocessableEntity{422};
constexpr int kHttpStatusInternalServerError{500};
constexpr int kHttpSuccessThreshold{300};
constexpr int kAlertPriority{1};
constexpr int kDefaultPriority{-1};

void logError(const std::string& reasonText, const Message& message) {
    constexpr MessageLogConfig kLogConfig{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };

    const std::optional<std::string> logLine = buildMessageLogLine(
        "pushover", MessageLogDirection::Incoming, message, kLogConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cerr << *logLine << " reason=\"" << escapeJsonString(reasonText) << "\"\n" << std::flush;
}

void logHttpError(const int statusCode, const std::string& reasonText, const Message& message) {
    constexpr MessageLogConfig kLogConfig{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };

    const std::optional<std::string> logLine = buildMessageLogLine(
        "pushover", MessageLogDirection::Incoming, message, kLogConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cerr << *logLine << " httpStatus=" << statusCode
              << " reason=\"" << escapeJsonString(reasonText) << "\"\n" << std::flush;
}

[[nodiscard]] std::optional<mqtt::json::JsonValue> tryParseJsonObject(const std::string& payloadText) {
    auto parsedValue = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedValue.has_value() || !parsedValue->is_object()) {
        return std::nullopt;
    }
    return parsedValue;
}

[[nodiscard]] std::optional<int> tryExtractJsonInteger(const mqtt::json::JsonValue& objectValue,
                                                       const std::string& keyText) {
    if (!objectValue.contains(keyText)) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& statusValue = objectValue.at(keyText);
    if (!statusValue.is_number()) {
        return std::nullopt;
    }

    const double numericValue = statusValue.as_number();
    if (!std::isfinite(numericValue) || std::floor(numericValue) != numericValue) {
        return std::nullopt;
    }

    constexpr auto kIntMin = static_cast<double>(std::numeric_limits<int>::min());
    constexpr auto kIntMax = static_cast<double>(std::numeric_limits<int>::max());
    if (numericValue < kIntMin || numericValue > kIntMax) {
        return std::nullopt;
    }

    return static_cast<int>(numericValue);
}

[[nodiscard]] std::optional<std::string> tryExtractJsonArray(const mqtt::json::JsonValue& objectValue,
                                                             const std::string& keyText) {
    if (!objectValue.contains(keyText)) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& arrayValue = objectValue.at(keyText);
    if (!arrayValue.is_array()) {
        return std::nullopt;
    }

    return arrayValue.stringify();
}

} // namespace

PushoverComponent::PushoverComponent(PushoverConfig config, PushoverRequestSender requestSender)
    : config_(std::move(config))
    , requestSender_(std::move(requestSender)) {
}

SubscriptionMap PushoverComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    for (const auto& subscriptionConfig : config_.subscriptions) {
        subscriptions[subscriptionConfig.topicFilter] = subscriptionConfig.qos;
    }
    return subscriptions;
}

void PushoverComponent::handleMessage(const Message& message) {
    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (!running_) {
            return;
        }
    }

    if (!requestSender_) {
        constexpr const char* kReasonText = "pushover request sender callback is missing";
        logError(kReasonText, message);
        publishStatusMessage(buildStatusMessage(
            kHttpStatusInternalServerError,
            message.reason(),
            kReasonText));
        return;
    }

    if (config_.devices.empty()) {
        constexpr const char* kReasonText = "pushover devices are not configured";
        logError(kReasonText, message);
        publishStatusMessage(buildStatusMessage(
            kHttpStatusUnprocessableEntity,
            message.reason(),
            kReasonText));
        return;
    }

    const std::string valueText = valueToText(message.value());
    const std::string title = message.topic() + " " + valueText;
    const std::string bodyText = formatReasonText(message.reason());
    const int priority = resolvePriority(message.value());

    for (const auto& device : config_.devices) {
        const std::string payload = buildPayload(
            config_.token,
            config_.user,
            device,
            title,
            bodyText,
            priority);

        try {
            const PushoverHttpResult result = requestSender_(config_.path, payload);
            const std::string resultReason = buildResultReason(result.statusCode, device, result.payload);
            if (result.statusCode >= kHttpSuccessThreshold) {
                logHttpError(result.statusCode, resultReason, message);
            }
            publishStatusMessage(buildStatusMessage(result.statusCode, message.reason(), resultReason));
        } catch (const std::exception& exceptionValue) {
            const std::string reasonText =
                std::format("pushover request failed for device {}: {}", device, exceptionValue.what());
            logError(reasonText, message);
            publishStatusMessage(buildStatusMessage(
                kHttpStatusInternalServerError,
                message.reason(),
                reasonText));
        } catch (...) {
            const std::string reasonText =
                std::format("pushover request failed for device {}: unknown", device);
            logError(reasonText, message);
            publishStatusMessage(buildStatusMessage(
                kHttpStatusInternalServerError,
                message.reason(),
                reasonText));
        }
    }
}

void PushoverComponent::run() {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    running_ = true;
}

void PushoverComponent::close() {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    running_ = false;
}

void PushoverComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> publishLock{publishMutex_};
    publishCallback_ = std::move(callback);
}

std::string PushoverComponent::formatReasonText(const ReasonList& reasons) {
    if (reasons.empty()) {
        return "no information";
    }

    const auto& firstReason = reasons.front();
    if (firstReason.timestamp.empty()) {
        return firstReason.message.empty() ? "no information" : firstReason.message;
    }

    if (firstReason.message.empty()) {
        return firstReason.timestamp;
    }

    return firstReason.timestamp + ": " + firstReason.message;
}

std::string PushoverComponent::valueToText(const Value& valueVariant) {
    if (std::holds_alternative<std::string>(valueVariant)) {
        return std::get<std::string>(valueVariant);
    }

    std::ostringstream stream{};
    stream << std::get<double>(valueVariant);
    return stream.str();
}

int PushoverComponent::resolvePriority(const Value& valueVariant) {
    if (std::holds_alternative<std::string>(valueVariant)
        && std::get<std::string>(valueVariant) == "alert") {
        return kAlertPriority;
    }

    return kDefaultPriority;
}

std::string PushoverComponent::buildPayload(
    const std::string& token,
    const std::string& user,
    const std::string& device,
    const std::string& title,
    const std::string& bodyText,
    const int priority) {
    mqtt::json::JsonValue payload = mqtt::json::JsonValue::object();
    payload["token"] = mqtt::json::JsonValue{token};
    payload["user"] = mqtt::json::JsonValue{user};
    payload["message"] = mqtt::json::JsonValue{bodyText};
    payload["priority"] = mqtt::json::JsonValue{static_cast<double>(priority)};
    payload["title"] = mqtt::json::JsonValue{title};
    payload["device"] = mqtt::json::JsonValue{device};
    return payload.stringify();
}

std::string PushoverComponent::buildResultReason(
    const int statusCode,
    const std::string& device,
    const std::string& payloadText) {
    const auto parsedObject = tryParseJsonObject(payloadText);
    const auto parsedStatusValue = parsedObject.has_value()
        ? tryExtractJsonInteger(*parsedObject, "status")
        : std::nullopt;
    const std::string parsedStatus =
        parsedStatusValue.has_value()
            ? std::to_string(*parsedStatusValue)
            : "unknown";

    if (statusCode < kHttpSuccessThreshold) {
        return std::format("pushover({}) status = {}", device, parsedStatus);
    }

    const std::string parsedErrors = parsedObject.has_value()
        ? tryExtractJsonArray(*parsedObject, "errors").value_or("[]")
        : "[]";
    return std::format(
        "pushover status({}) = {} errors = {}",
        device,
        parsedStatus,
        mqtt::helper::trim(parsedErrors));
}

Message PushoverComponent::buildStatusMessage(
    const int statusCode,
    const ReasonList& sourceReasons,
    const std::string& resultReason) {
    const std::string topicName = (statusCode < kHttpSuccessThreshold)
        ? "$MONITOR/pushover/success"
        : "$MONITOR/pushover/error";

    Message statusMessage{topicName, static_cast<double>(statusCode), Qos::AtLeastOnce, false};
    for (const auto& reasonEntry : sourceReasons) {
        statusMessage.addReason(reasonEntry.message, reasonEntry.timestamp);
    }
    statusMessage.addReason(resultReason);
    return statusMessage;
}

void PushoverComponent::publishStatusMessage(const Message& statusMessage) const {
    std::lock_guard<std::mutex> publishLock{publishMutex_};
    if (!publishCallback_) {
        std::cerr << "pushover[error] publish_callback_missing"
                  << " topic=" << statusMessage.topic() << '\n' << std::flush;
        return;
    }

    const PublishResult result = publishCallback_(statusMessage);
    if (result.success) {
        return;
    }

    std::cerr << "pushover[error] status_publish_failed"
              << " topic=" << statusMessage.topic()
              << " reason=" << result.reason
              << '\n' << std::flush;
}

} // namespace yaha
