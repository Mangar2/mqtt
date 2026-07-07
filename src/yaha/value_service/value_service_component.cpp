#include "yaha/value_service/value_service_component.h"

#include "json/json_value.h"
#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"

#include "httplib.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr int k_http_ok_status{200};
constexpr std::size_t k_set_suffix_size{4U};
constexpr std::size_t k_max_pending_publish_attempts{3U};
constexpr int k_file_store_connect_timeout_seconds{1};
constexpr int k_file_store_read_timeout_seconds{1};
constexpr int k_file_store_write_timeout_seconds{1};

[[nodiscard]] std::string publishFailureCategoryToText(const PublishFailureCategory categoryValue) {
    switch (categoryValue) {
    case PublishFailureCategory::None:
        return "none";
    case PublishFailureCategory::Disconnected:
        return "disconnected";
    case PublishFailureCategory::AckTimeout:
        return "ack_timeout";
    case PublishFailureCategory::WriteFailed:
        return "write_failed";
    case PublishFailureCategory::CallbackMissing:
        return "callback_missing";
    case PublishFailureCategory::Unknown:
        return "unknown";
    }

    return "unknown";
}

[[nodiscard]] bool endsWithSetSuffix(const std::string& textValue) {
    return textValue.size() >= k_set_suffix_size
        && textValue.compare(textValue.size() - k_set_suffix_size, k_set_suffix_size, "/set") == 0;
}

void configureFileStoreClientTimeouts(httplib::Client* client) {
    if (client == nullptr) {
        return;
    }

    client->set_connection_timeout(k_file_store_connect_timeout_seconds, 0);
    client->set_read_timeout(k_file_store_read_timeout_seconds, 0);
    client->set_write_timeout(k_file_store_write_timeout_seconds, 0);
}

} // namespace

ValueServiceComponent::ValueServiceComponent(ValueServiceConfig config)
    : config_(std::move(config)) {
}

SubscriptionMap ValueServiceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({config_.monitorTopicPrefix + "/#", config_.subscribeQos});

    std::lock_guard<std::mutex> lock{stateMutex_};
    for (const auto& [key, value] : values_) {
        (void)value;
        subscriptions.insert({key + "/set", config_.subscribeQos});
    }

    return subscriptions;
}

void ValueServiceComponent::handleMessage(const Message& message) {
    processPendingPublishQueue();
    logIncomingMessageIfEnabled(message);

    if (isMonitoringTopic(message.topic())) {
        handleMonitoringMessage(message);
        return;
    }

    if (isSetTopic(message.topic())) {
        handleSetMessage(message);
    }
}

void ValueServiceComponent::run() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
    }

    if (config_.fileStoreEnabled) {
        (void)loadValuesFromFileStore();
    }

    publishAllValuesSnapshot(std::string{"loaded from valuestore on startup"});
    processPendingPublishQueue();
}

void ValueServiceComponent::close() {
    std::lock_guard<std::mutex> lock{stateMutex_};
    running_ = false;
}

void ValueServiceComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{publishMutex_};
    publishCallback_ = std::move(callback);
}

bool ValueServiceComponent::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

std::size_t ValueServiceComponent::valueCount() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return values_.size();
}

std::optional<Value> ValueServiceComponent::valueForKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    const auto iterator = values_.find(key);
    if (iterator == values_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

bool ValueServiceComponent::loadValuesFromFileStore() {
    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    configureFileStoreClientTimeouts(&client);
    const auto response = client.Get(config_.valuesKeyPath);
    if (!response || response->status != k_http_ok_status) {
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cout << "value_service[error] op=filestore_get path=" << config_.valuesKeyPath
                  << " status=" << statusText
                  << " reason=load_values_failed"
                  << '\n'
                  << std::flush;
        return false;
    }

    ValueMap parsedValues{};
    if (!parseValueMapJson(response->body, parsedValues)) {
        std::cout << "value_service[error] op=filestore_get path=" << config_.valuesKeyPath
                  << " status=" << response->status
                  << " reason=invalid_json"
                  << '\n'
                  << std::flush;
        return false;
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    values_ = std::move(parsedValues);
    return true;
}

bool ValueServiceComponent::persistValuesToFileStore() const {
    if (!config_.fileStoreEnabled) {
        return true;
    }

    const std::string payloadText = [this]() {
        std::lock_guard<std::mutex> lock{stateMutex_};
        return serializeValueMap(values_);
    }();

    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    configureFileStoreClientTimeouts(&client);
    const auto response = client.Post(config_.valuesKeyPath, payloadText, "application/json");
    if (!response || response->status != k_http_ok_status) {
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cout << "value_service[error] op=filestore_post path=" << config_.valuesKeyPath
                  << " status=" << statusText
                  << " reason=persist_values_failed"
                  << '\n'
                  << std::flush;
        return false;
    }

    return true;
}

void ValueServiceComponent::handleMonitoringMessage(const Message& message) {
    if (!std::holds_alternative<std::string>(message.value())) {
        return;
    }

    const std::optional<std::string> source = extractJsonStringField(
        std::get<std::string>(message.value()),
        "source");
    if (source.has_value() && *source == "filesystem-watch") {
        return;
    }

    const std::optional<std::string> keyPath = extractJsonStringField(
        std::get<std::string>(message.value()),
        "keyPath");
    if (!keyPath.has_value() || *keyPath != config_.valuesKeyPath) {
        return;
    }

    if (!loadValuesFromFileStore()) {
        std::cout << "value_service[error] op=monitor_reload path=" << config_.valuesKeyPath
                  << " reason=reload_failed"
                  << '\n'
                  << std::flush;
        return;
    }

    publishAllValuesSnapshot(std::string{"reloaded after valuestore file change"});
}

void ValueServiceComponent::handleSetMessage(const Message& message) {
    const std::string key = stripSetSuffix(message.topic());
    if (key.empty()) {
        return;
    }

    if (!isSupportedValueType(message.value())) {
        return;
    }

    Value normalizedValue = message.value();
    if (std::holds_alternative<double>(normalizedValue)) {
        const double currentNumber = std::get<double>(normalizedValue);
        normalizedValue = static_cast<double>(static_cast<std::int64_t>(currentNumber));
    }

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        values_[key] = normalizedValue;
    }

    publishRetainedValue(key, normalizedValue);
    if (!persistValuesToFileStore()) {
        std::cout << "value_service[error] op=set_persist key=" << key
                  << " reason=persist_failed"
                  << '\n'
                  << std::flush;
    }
}

bool ValueServiceComponent::isMonitoringTopic(const std::string& topicName) const {
    return topicName.starts_with(config_.monitorTopicPrefix + "/");
}

bool ValueServiceComponent::isSetTopic(const std::string& topicName) {
    return endsWithSetSuffix(topicName);
}

std::string ValueServiceComponent::stripSetSuffix(const std::string& topicName) {
    if (!isSetTopic(topicName)) {
        return topicName;
    }
    return topicName.substr(0U, topicName.size() - k_set_suffix_size);
}

std::optional<std::string> ValueServiceComponent::extractJsonStringField(
    const std::string& payload,
    const std::string& fieldName) {
    const auto parsedPayload = mqtt::json::JsonValue::try_parse(payload);
    if (!parsedPayload.has_value() || !parsedPayload->is_object()) {
        return std::nullopt;
    }

    if (!parsedPayload->contains(fieldName)) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& fieldValue = parsedPayload->at(fieldName);
    if (!fieldValue.is_string()) {
        return std::nullopt;
    }

    return fieldValue.as_string();
}

bool ValueServiceComponent::isSupportedValueType(const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return true;
    }

    const double numberValue = std::get<double>(value);
    if (!std::isfinite(numberValue)) {
        return false;
    }

    const double integralPart = std::trunc(numberValue);
    if (integralPart != numberValue) {
        return false;
    }

    return numberValue >= static_cast<double>(std::numeric_limits<std::int64_t>::min())
        && numberValue <= static_cast<double>(std::numeric_limits<std::int64_t>::max());
}

std::string ValueServiceComponent::serializeValueMap(const ValueMap& values) {
    mqtt::json::JsonValue rootValue = mqtt::json::JsonValue::object();
    for (const auto& [key, value] : values) {
        if (std::holds_alternative<std::string>(value)) {
            rootValue[key] = mqtt::json::JsonValue{std::get<std::string>(value)};
        } else {
            const auto integerValue = static_cast<std::int64_t>(std::get<double>(value));
            rootValue[key] = mqtt::json::JsonValue{static_cast<double>(integerValue)};
        }
    }

    return rootValue.stringify();
}

bool ValueServiceComponent::parseValueMapJson(const std::string& jsonText, ValueMap& output) {
    const auto parsedValue = mqtt::json::JsonValue::try_parse(jsonText);
    if (!parsedValue.has_value() || !parsedValue->is_object()) {
        return false;
    }

    ValueMap parsedMap{};
    for (const auto& [keyText, entryValue] : parsedValue->as_object()) {
        if (entryValue.is_string()) {
            parsedMap[keyText] = entryValue.as_string();
            continue;
        }

        if (!entryValue.is_number()) {
            return false;
        }

        const double numericValue = entryValue.as_number();
        if (!std::isfinite(numericValue) || std::trunc(numericValue) != numericValue) {
            return false;
        }

        if (numericValue < static_cast<double>(std::numeric_limits<std::int64_t>::min())
            || numericValue > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }

        parsedMap[keyText] = numericValue;
    }

    output = std::move(parsedMap);
    return true;
}

void ValueServiceComponent::logIncomingMessageIfEnabled(const Message& message) const {
    const MessageLogConfig logConfig{
        .enableIncoming = config_.logIncomingMessages,
        .enableOutgoing = false,
        .includeReasonChain = config_.logReason,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "value_service",
        MessageLogDirection::Incoming,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void ValueServiceComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = config_.logOutgoingMessages,
        .includeReasonChain = config_.logReason,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "value_service",
        MessageLogDirection::Outgoing,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void ValueServiceComponent::publishRetainedValue(
    const std::string& key,
    const Value& value,
    const std::optional<std::string>& reasonText) const {
    Message message{key, value, config_.subscribeQos, true};
    if (reasonText.has_value()) {
        message.addReason(*reasonText);
    }

    if (!tryPublishMessage(message, "retained_value")) {
        enqueuePendingPublish(message, "retained_value");
    }
}

void ValueServiceComponent::publishAllValuesSnapshot(const std::optional<std::string>& reasonText) const {
    ValueMap valuesSnapshot{};
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        valuesSnapshot = values_;
    }

    for (const auto& [key, value] : valuesSnapshot) {
        publishRetainedValue(key, value, reasonText);
    }

    processPendingPublishQueue();
}

bool ValueServiceComponent::tryPublishMessage(const Message& message,
                                              const std::string& channelText) const {
    PublishCallback callback{};
    {
        std::lock_guard<std::mutex> lock{publishMutex_};
        callback = publishCallback_;
    }

    if (!callback) {
        logOutgoingFailure(message, channelText, "callback_missing");
        return false;
    }

    try {
        const PublishResult publishResult = callback(message);
        if (!publishResult.success) {
            const std::string reasonText = publishResult.reason.empty()
                ? "unspecified"
                : publishResult.reason;
            logOutgoingFailure(
                message,
                publishFailureCategoryToText(publishResult.category),
                reasonText);
            return false;
        }

        logOutgoingMessageIfEnabled(message);
        return true;
    } catch (const std::exception& exceptionValue) {
        logOutgoingFailure(message, channelText, exceptionValue.what());
    } catch (...) {
        logOutgoingFailure(message, channelText, "unknown");
    }

    return false;
}

void ValueServiceComponent::enqueuePendingPublish(const Message& message,
                                                  const std::string& channelText) const {
    std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
    pendingPublishQueue_.push_back(PendingPublishEntry{
        .message = message.clone(),
        .channelText = channelText,
        .attemptCount = 0U});
}

void ValueServiceComponent::processPendingPublishQueue() const {
    std::deque<PendingPublishEntry> pendingBatch{};
    {
        std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
        if (pendingPublishQueue_.empty()) {
            return;
        }

        pendingBatch.swap(pendingPublishQueue_);
    }

    for (auto& pendingEntry : pendingBatch) {
        if (tryPublishMessage(pendingEntry.message, pendingEntry.channelText)) {
            continue;
        }

        pendingEntry.attemptCount += 1U;
        if (pendingEntry.attemptCount >= k_max_pending_publish_attempts) {
            logOutgoingFailure(
                pendingEntry.message,
                "retry_exhausted",
                pendingEntry.channelText);
            continue;
        }

        std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
        pendingPublishQueue_.push_back(std::move(pendingEntry));
    }
}

void ValueServiceComponent::logOutgoingFailure(const Message& message,
                                               const std::string& categoryText,
                                               const std::string& reasonText) {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "value_service",
        MessageLogDirection::Outgoing,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cerr << *logLine
              << " event=publish_failed"
              << " category=" << categoryText
              << " reason=\"" << escapeJsonString(reasonText) << '\"'
              << '\n'
              << std::flush;
}

} // namespace yaha
