#include "yaha/value_service/value_service_component.h"

#include "httplib.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr int k_http_ok_status{200};
constexpr std::size_t k_set_suffix_size{4U};
constexpr std::size_t k_json_escape_reserve_extra{8U};
constexpr int k_decimal_base{10};
constexpr std::size_t k_max_pending_publish_attempts{3U};

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

[[nodiscard]] std::string valueToLogText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return textStream.str();
}

[[nodiscard]] std::string qosToLogText(const Qos qosValue) {
    switch (qosValue) {
    case Qos::AtMostOnce:
        return "0";
    case Qos::AtLeastOnce:
        return "1";
    case Qos::ExactlyOnce:
        return "2";
    }

    return "unknown";
}

void logMessage(const char* directionText, const Message& message) {
    std::cout << "value_service[" << directionText << "] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value());

    if (!message.reason().empty()) {
        std::cout << " reason=\"" << message.reason().front().message << '\"';
    }

    std::cout << '\n' << std::flush;
}

[[nodiscard]] bool startsWithText(const std::string& textValue, const std::string& prefix) {
    return textValue.size() >= prefix.size() && textValue.compare(0U, prefix.size(), prefix) == 0;
}

[[nodiscard]] bool endsWithSetSuffix(const std::string& textValue) {
    return textValue.size() >= k_set_suffix_size
        && textValue.compare(textValue.size() - k_set_suffix_size, k_set_suffix_size, "/set") == 0;
}

[[nodiscard]] bool parseJsonStringToken(
    const std::string& jsonText,
    std::size_t& parseIndex,
    std::string& output) {
    if (parseIndex >= jsonText.size() || jsonText[parseIndex] != '"') {
        return false;
    }
    parseIndex += 1U;

    std::string valueText{};
    while (parseIndex < jsonText.size()) {
        const char currentChar = jsonText[parseIndex++];
        if (currentChar == '"') {
            output = std::move(valueText);
            return true;
        }

        if (currentChar == '\\') {
            if (parseIndex >= jsonText.size()) {
                return false;
            }
            const char escapedChar = jsonText[parseIndex++];
            switch (escapedChar) {
            case '"':
            case '\\':
            case '/':
                valueText.push_back(escapedChar);
                break;
            case 'n':
                valueText.push_back('\n');
                break;
            case 'r':
                valueText.push_back('\r');
                break;
            case 't':
                valueText.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        valueText.push_back(currentChar);
    }

    return false;
}

[[nodiscard]] std::string jsonEscape(const std::string& input) {
    std::string result{};
    result.reserve(input.size() + k_json_escape_reserve_extra);
    for (const char currentChar : input) {
        switch (currentChar) {
        case '"':
        case '\\':
            result.push_back('\\');
            result.push_back(currentChar);
            break;
        case '\n':
            result.append("\\n");
            break;
        case '\r':
            result.append("\\r");
            break;
        case '\t':
            result.append("\\t");
            break;
        default:
            result.push_back(currentChar);
            break;
        }
    }
    return result;
}

void skipWhitespace(const std::string& text, std::size_t& parseIndex) {
    while (parseIndex < text.size() && std::isspace(static_cast<unsigned char>(text[parseIndex])) != 0) {
        parseIndex += 1U;
    }
}

[[nodiscard]] bool consumeChar(const std::string& text, std::size_t& parseIndex, const char expectedChar) {
    skipWhitespace(text, parseIndex);
    if (parseIndex >= text.size() || text[parseIndex] != expectedChar) {
        return false;
    }
    parseIndex += 1U;
    return true;
}

[[nodiscard]] bool parseJsonIntegerToken(
    const std::string& text,
    std::size_t& parseIndex,
    std::int64_t& output) {
    skipWhitespace(text, parseIndex);
    if (parseIndex >= text.size()) {
        return false;
    }

    std::size_t tokenEnd = parseIndex;
    if (text[tokenEnd] == '-') {
        tokenEnd += 1U;
    }

    const std::size_t firstDigitIndex = tokenEnd;
    while (tokenEnd < text.size() && std::isdigit(static_cast<unsigned char>(text[tokenEnd])) != 0) {
        tokenEnd += 1U;
    }

    if (tokenEnd == firstDigitIndex) {
        return false;
    }

    const std::string numberText = text.substr(parseIndex, tokenEnd - parseIndex);
    std::size_t consumedChars = 0U;
    std::int64_t parsedInteger = 0;
    try {
        parsedInteger = std::stoll(numberText, &consumedChars, k_decimal_base);
    } catch (...) {
        return false;
    }

    if (consumedChars != numberText.size()) {
        return false;
    }

    output = parsedInteger;
    parseIndex = tokenEnd;
    return true;
}

[[nodiscard]] bool parseValueMapEntry(
    const std::string& jsonText,
    std::size_t& parseIndex,
    std::string& key,
    Value& value) {
    skipWhitespace(jsonText, parseIndex);
    if (!parseJsonStringToken(jsonText, parseIndex, key)) {
        return false;
    }

    if (!consumeChar(jsonText, parseIndex, ':')) {
        return false;
    }

    skipWhitespace(jsonText, parseIndex);
    if (parseIndex >= jsonText.size()) {
        return false;
    }

    if (jsonText[parseIndex] == '"') {
        std::string stringValue{};
        if (!parseJsonStringToken(jsonText, parseIndex, stringValue)) {
            return false;
        }
        value = std::move(stringValue);
        return true;
    }

    std::int64_t parsedInteger = 0;
    if (!parseJsonIntegerToken(jsonText, parseIndex, parsedInteger)) {
        return false;
    }
    value = static_cast<double>(parsedInteger);
    return true;
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
    logMessage("in", message);

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
    return startsWithText(topicName, config_.monitorTopicPrefix + "/");
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
    const std::string keyToken = "\"" + fieldName + "\"";
    std::size_t keyPosition = payload.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    keyPosition = payload.find(':', keyPosition + keyToken.size());
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t parseIndex = keyPosition + 1U;
    while (parseIndex < payload.size()
        && std::isspace(static_cast<unsigned char>(payload[parseIndex])) != 0) {
        parseIndex += 1U;
    }

    std::string valueText{};
    if (!parseJsonStringToken(payload, parseIndex, valueText)) {
        return std::nullopt;
    }

    return valueText;
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
    std::string jsonText{"{"};
    bool firstEntry = true;
    for (const auto& [key, value] : values) {
        if (!firstEntry) {
            jsonText.push_back(',');
        }
        firstEntry = false;
        jsonText.append("\"");
        jsonText.append(jsonEscape(key));
        jsonText.append("\":");

        if (std::holds_alternative<std::string>(value)) {
            jsonText.push_back('"');
            jsonText.append(jsonEscape(std::get<std::string>(value)));
            jsonText.push_back('"');
        } else {
            const auto integerValue = static_cast<std::int64_t>(std::get<double>(value));
            jsonText.append(std::to_string(integerValue));
        }
    }
    jsonText.push_back('}');
    return jsonText;
}

bool ValueServiceComponent::parseValueMapJson(const std::string& jsonText, ValueMap& output) {
    output.clear();

    std::size_t parseIndex = 0U;
    if (!consumeChar(jsonText, parseIndex, '{')) {
        return false;
    }

    while (true) {
        skipWhitespace(jsonText, parseIndex);

        if (parseIndex < jsonText.size() && jsonText[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        std::string keyText{};
        Value parsedValue{};
        if (!parseValueMapEntry(jsonText, parseIndex, keyText, parsedValue)) {
            return false;
        }

        output[keyText] = parsedValue;

        skipWhitespace(jsonText, parseIndex);

        if (parseIndex < jsonText.size() && jsonText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }

        if (parseIndex < jsonText.size() && jsonText[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        return false;
    }

    skipWhitespace(jsonText, parseIndex);

    return parseIndex == jsonText.size();
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

        logMessage("out", message);
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
    pendingPublishQueue_.push_back(PendingPublishEntry{message.clone(), channelText, 0U});
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
    std::cout << "value_service[out-fail] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << " category=" << categoryText
              << " reason=" << reasonText
              << '\n'
              << std::flush;
}

} // namespace yaha
