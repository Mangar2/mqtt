#include "yaha/pushover/pushover_component.h"

#include "yaha/message/message_payload_codec.h"

#include <cctype>
#include <exception>
#include <format>
#include <iostream>
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

void logError(const std::string& reasonText, const std::string& topicName) {
    std::cerr << "pushover[error]"
              << " topic=" << topicName
              << " reason=" << reasonText
              << '\n' << std::flush;
}

void logHttpError(const int statusCode, const std::string& reasonText, const std::string& topicName) {
    std::cerr << "pushover[error]"
              << " topic=" << topicName
              << " httpStatus=" << statusCode
              << " reason=" << reasonText
              << '\n' << std::flush;
}

[[nodiscard]] std::string trimCopy(const std::string_view textValue) {
    std::size_t beginIndex = 0U;
    while (beginIndex < textValue.size()
           && std::isspace(static_cast<unsigned char>(textValue[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    std::size_t endIndex = textValue.size();
    while (endIndex > beginIndex
           && std::isspace(static_cast<unsigned char>(textValue[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return std::string{textValue.substr(beginIndex, endIndex - beginIndex)};
}

[[nodiscard]] std::optional<int> tryExtractJsonInteger(const std::string& payloadText, const std::string& keyText) {
    const std::string token = "\"" + keyText + "\"";
    const std::size_t keyPosition = payloadText.find(token);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t parsePosition = keyPosition + token.size();
    while (parsePosition < payloadText.size()
           && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size() || payloadText[parsePosition] != ':') {
        return std::nullopt;
    }
    parsePosition += 1U;

    while (parsePosition < payloadText.size()
           && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size()) {
        return std::nullopt;
    }

    std::size_t endPosition = parsePosition;
    if (payloadText[endPosition] == '-') {
        endPosition += 1U;
    }

    const std::size_t digitStart = endPosition;
    while (endPosition < payloadText.size()
           && std::isdigit(static_cast<unsigned char>(payloadText[endPosition])) != 0) {
        endPosition += 1U;
    }

    if (digitStart == endPosition) {
        return std::nullopt;
    }

    try {
        return std::stoi(payloadText.substr(parsePosition, endPosition - parsePosition));
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> tryExtractJsonArray(const std::string& payloadText, const std::string& keyText) {
    const std::string token = "\"" + keyText + "\"";
    const std::size_t keyPosition = payloadText.find(token);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t parsePosition = keyPosition + token.size();
    while (parsePosition < payloadText.size()
           && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size() || payloadText[parsePosition] != ':') {
        return std::nullopt;
    }
    parsePosition += 1U;

    while (parsePosition < payloadText.size()
           && std::isspace(static_cast<unsigned char>(payloadText[parsePosition])) != 0) {
        parsePosition += 1U;
    }

    if (parsePosition >= payloadText.size() || payloadText[parsePosition] != '[') {
        return std::nullopt;
    }

    int depth = 0;
    std::size_t endPosition = parsePosition;
    while (endPosition < payloadText.size()) {
        const char currentChar = payloadText[endPosition];
        if (currentChar == '[') {
            depth += 1;
        } else if (currentChar == ']') {
            depth -= 1;
            if (depth == 0) {
                return payloadText.substr(parsePosition, (endPosition - parsePosition) + 1U);
            }
        }
        endPosition += 1U;
    }

    return std::nullopt;
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
        logError(kReasonText, message.topic());
        publishStatusMessage(buildStatusMessage(
            kHttpStatusInternalServerError,
            message.reason(),
            kReasonText));
        return;
    }

    if (config_.devices.empty()) {
        constexpr const char* kReasonText = "pushover devices are not configured";
        logError(kReasonText, message.topic());
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
                logHttpError(result.statusCode, resultReason, message.topic());
            }
            publishStatusMessage(buildStatusMessage(result.statusCode, message.reason(), resultReason));
        } catch (const std::exception& exceptionValue) {
            const std::string reasonText =
                std::format("pushover request failed for device {}: {}", device, exceptionValue.what());
            logError(reasonText, message.topic());
            publishStatusMessage(buildStatusMessage(
                kHttpStatusInternalServerError,
                message.reason(),
                reasonText));
        } catch (...) {
            const std::string reasonText =
                std::format("pushover request failed for device {}: unknown", device);
            logError(reasonText, message.topic());
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
    std::ostringstream payload{};
        payload << R"({"token":")" << escapeJsonString(token)
            << R"(","user":")" << escapeJsonString(user)
            << R"(","message":")" << escapeJsonString(bodyText)
            << R"(","priority":)" << priority
            << R"(,"title":")" << escapeJsonString(title)
            << R"(","device":")" << escapeJsonString(device)
            << R"("})";
    return payload.str();
}

std::string PushoverComponent::buildResultReason(
    const int statusCode,
    const std::string& device,
    const std::string& payloadText) {
    const auto parsedStatusValue = tryExtractJsonInteger(payloadText, "status");
    const std::string parsedStatus =
        parsedStatusValue.has_value()
            ? std::to_string(*parsedStatusValue)
            : "unknown";

    if (statusCode < kHttpSuccessThreshold) {
        return std::format("pushover({}) status = {}", device, parsedStatus);
    }

    const std::string parsedErrors =
        tryExtractJsonArray(payloadText, "errors").value_or("[]");
    return std::format(
        "pushover status({}) = {} errors = {}",
        device,
        parsedStatus,
        trimCopy(parsedErrors));
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
