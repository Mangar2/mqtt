#include "yaha/rs485_interface/rs485_interface_component.h"

#include "yaha/message/message_log_service.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <sstream>
#include <thread>
#include <utility>

namespace yaha {
namespace {

constexpr std::uint32_t k_default_blink_cycles{1U};
constexpr std::uint32_t k_blink_toggle_multiplier{2U};
constexpr double k_integer_epsilon{1e-9};
constexpr std::uint32_t k_interruptible_sleep_quantum_ms{50U};
constexpr std::size_t k_legacy_log_prefix_min_length{42U};
constexpr const char* k_trace_topic_set{"$SYS/rs485Interface/trace/set"};
constexpr const char* k_monitor_trace_topic_set{"$MONITOR/rs485Interface/trace/set"};

[[nodiscard]] bool shouldTraceError(const std::string& traceLevel) {
    // Preserve legacy singular/plural mismatch quirk ("error" vs configured "errors").
    return traceLevel == "error" || traceLevel == "messages" || traceLevel == "internal";
}

[[nodiscard]] bool shouldTraceMessage(const std::string& traceLevel, const bool isInternalMessage) {
    return (traceLevel == "messages" && !isInternalMessage) || traceLevel == "internal";
}

[[nodiscard]] std::string toLowerHexString(const std::uint8_t value) {
    std::ostringstream stream{};
    stream << std::hex << std::nouppercase << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(value);
    return stream.str();
}

[[nodiscard]] std::string formatHexForLegacyLog(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t startIndex,
    const std::size_t messageSize) {
    if (startIndex >= byteArray.size() || messageSize == 0U) {
        return {};
    }

    const std::size_t available = byteArray.size() - startIndex;
    const std::size_t amount = std::min(available, messageSize);

    std::ostringstream stream{};
    stream << "([" << byteArray.size() << "] ";
    for (std::size_t index = 0U; index < amount; ++index) {
        stream << ' ' << toLowerHexString(byteArray[startIndex + index]);
    }
    stream << ')';
    return stream.str();
}

[[nodiscard]] std::string currentLocalTimeForLegacyLog() {
    const std::time_t now = std::time(nullptr);
    const std::tm* localTime = std::localtime(&now);
    if (localTime == nullptr) {
        return "00:00:00";
    }

    std::ostringstream stream{};
    stream << std::put_time(localTime, "%X");
    return stream.str();
}

[[nodiscard]] std::string valueToLegacyLogText(const Rs485SerialMessage& message) {
    if (message.command == k_rs485_token_command) {
        const auto stateValue = static_cast<std::uint8_t>(message.value);
        if (stateValue == static_cast<std::uint8_t>(Rs485StateResult::EnableSend)) {
            return "enable send";
        }
        if (stateValue == static_cast<std::uint8_t>(Rs485StateResult::RegistrationInfo)) {
            return "reg. info";
        }
        if (stateValue == static_cast<std::uint8_t>(Rs485StateResult::RegistrationRequest)) {
            return "reg. request";
        }
    }

    std::ostringstream stream{};
    stream << message.value;
    return stream.str();
}

[[nodiscard]] std::string buildLegacyLoggingInfo(const Rs485SerialMessage& message) {
    const int replyBit = message.reply ? 1 : 0;
    std::ostringstream stream{};
    stream << currentLocalTimeForLegacyLog()
           << ' ' << static_cast<int>(message.sender)
           << " => " << static_cast<int>(message.receiver)
           << " (r:" << replyBit << "): "
           << message.command
           << " = " << valueToLegacyLogText(message);

    std::string output = stream.str();
    while (output.size() < k_legacy_log_prefix_min_length) {
        output.push_back(' ');
    }

    const std::vector<std::uint8_t> encoded = encodeRs485SerialMessage(message);
    output += formatHexForLegacyLog(encoded, 0U, message.length);
    return output;
}

[[nodiscard]] std::string joinTopicPath(const std::string& base, const std::string& suffix) {
    if (base.empty()) {
        return suffix;
    }
    if (suffix.empty()) {
        return base;
    }

    const bool baseEndsWithSlash = base.back() == '/';
    const bool suffixStartsWithSlash = suffix.front() == '/';
    if (baseEndsWithSlash && suffixStartsWithSlash) {
        return base + suffix.substr(1U);
    }
    if (!baseEndsWithSlash && !suffixStartsWithSlash) {
        return base + "/" + suffix;
    }
    return base + suffix;
}

[[nodiscard]] Value toggledValueFromState(const std::string& cachedState) {
    if (cachedState == "on") {
        return std::string{"off"};
    }
    return std::string{"on"};
}

void sleepInterruptible(
    const std::atomic<bool>& running,
    const std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running.load() && std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto step = std::min(
            remaining,
            std::chrono::milliseconds{k_interruptible_sleep_quantum_ms});
        if (step.count() > 0) {
            std::this_thread::sleep_for(step);
        }
    }
}

} // namespace

Rs485InterfaceComponent::Rs485InterfaceComponent(Rs485InterfaceConfig config)
    : config_(std::move(config))
    , mapper_(config_)
    , scheduler_(config_.myAddress, config_.maxVersion, config_.tickDelayMs) {
    scheduler_.setSendCallback([this](const Rs485SerialMessage& message) {
        onSchedulerSend(message);
    });
}

Rs485InterfaceComponent::~Rs485InterfaceComponent() {
    close();
}

SubscriptionMap Rs485InterfaceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};

    std::map<std::string, bool> startTopics{};
    for (const auto& [addressTopic, address] : config_.addresses) {
        (void)address;
        startTopics[deriveWildcardStartTopic(addressTopic)] = true;
    }

    for (const auto& [startTopic, included] : startTopics) {
        (void)included;
        for (const auto& [command, settingSuffix] : config_.settings) {
            (void)command;
            subscriptions[joinTopicPath(startTopic, settingSuffix) + "/set"] = config_.subscribeQos;
        }
    }

    for (const auto& [topic, mapping] : config_.topics) {
        (void)mapping;
        subscriptions[topic + "/+"] = config_.subscribeQos;
    }

    subscriptions["$SYS/rs485Interface/#"] = config_.subscribeQos;
    subscriptions["$MONITOR/rs485Interface/#"] = config_.subscribeQos;

    return subscriptions;
}

void Rs485InterfaceComponent::handleMessage(const Message& message) {
    logIncomingMessageIfEnabled(message);

    const std::string topicLower = toLowerCopy(message.topic());
    if (topicLower == toLowerCopy(k_trace_topic_set) || topicLower == toLowerCopy(k_monitor_trace_topic_set)) {
        if (std::holds_alternative<std::string>(message.value())) {
            config_.traceLevel = std::get<std::string>(message.value());
        }
        return;
    }

    Message actionMessage{message.topic(), message.value(), message.qos(), message.retain(), message.dup()};
    addReasonsPreservingOrder(actionMessage, message.reason());
    actionMessage.addReason("received by RS485Interface service");
    addReceivedMessage(actionMessage);
    processActionMessage(actionMessage);
}

void Rs485InterfaceComponent::run() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    schedulerThread_ = std::thread([this]() {
        runSchedulerLoop();
    });
    timeOfDayThread_ = std::thread([this]() {
        runTimeOfDayLoop();
    });

    std::cout << "rs485 service is running" << '\n' << std::flush;
}

void Rs485InterfaceComponent::close() {
    running_.store(false);

    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }

    if (timeOfDayThread_.joinable()) {
        timeOfDayThread_.join();
    }

    std::vector<std::thread> actionThreads{};
    {
        std::lock_guard<std::mutex> lock{actionThreadsMutex_};
        actionThreads.swap(actionThreads_);
    }
    for (auto& thread : actionThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::cout << "rs485 service closed" << '\n' << std::flush;
}

void Rs485InterfaceComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{publishMutex_};
    publishCallback_ = std::move(callback);
}

void Rs485InterfaceComponent::setSerialSendCallback(SerialSendCallback callback) {
    std::lock_guard<std::mutex> lock{serialSendMutex_};
    serialSendCallback_ = std::move(callback);
}

void Rs485InterfaceComponent::feedSerialBytes(const std::vector<std::uint8_t>& byteChunk) {
    const auto readResults = Rs485StreamReader::read(byteChunk);
    for (const auto& readResult : readResults) {
        if (!readResult.message.has_value()) {
            if (shouldTraceError(config_.traceLevel) && !readResult.error.empty()) {
                std::cout << readResult.error << '\n' << std::flush;
            }
            continue;
        }

        const Rs485SerialMessage& serialMessage = *readResult.message;
        if (shouldTraceMessage(config_.traceLevel, serialMessage.isInternal())) {
            try {
                std::cout << buildLegacyLoggingInfo(serialMessage) << '\n' << std::flush;
            } catch (const std::exception& exceptionValue) {
                if (shouldTraceError(config_.traceLevel)) {
                    std::cout << exceptionValue.what() << '\n' << std::flush;
                }
            }
        }

        const bool sendToBroker = scheduler_.processReceivedMessage(serialMessage);
        if (!sendToBroker) {
            continue;
        }

        try {
            auto mappedMessages = mapper_.toMqttMessages(serialMessage);
            for (auto& mappedMessage : mappedMessages) {
                mappedMessage.addReason("received from arduino");
                updateTopicStateCache(mappedMessage);
                matchAndUpdateReplyMessage(mappedMessage);
            }
            publishMappedMessages(mappedMessages);
        } catch (const std::exception& exceptionValue) {
            if (shouldTraceError(config_.traceLevel)) {
                std::cout << exceptionValue.what() << '\n' << std::flush;
            }
        }
    }
}

std::string Rs485InterfaceComponent::toLowerCopy(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char characterValue) {
        return static_cast<char>(std::tolower(characterValue));
    });
    return text;
}

bool Rs485InterfaceComponent::endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.ends_with(suffix);
}

std::string Rs485InterfaceComponent::removeSuffix(const std::string& text, const std::string& suffix) {
    if (!endsWith(text, suffix)) {
        return text;
    }
    return text.substr(0U, text.size() - suffix.size());
}

bool Rs485InterfaceComponent::isActionTopicSegment(const std::string& segment) {
    return segment == "set" || segment == "get" || segment == "temporary" || segment == "blink";
}

std::optional<std::string> Rs485InterfaceComponent::deriveReplyTopicForMatcher(const std::string& topic) {
    const std::string topicLower = toLowerCopy(topic);
    const std::size_t separatorIndex = topicLower.rfind('/');
    if (separatorIndex == std::string::npos) {
        return std::nullopt;
    }

    const std::string lastSegment = topicLower.substr(separatorIndex + 1U);
    if (!isActionTopicSegment(lastSegment)) {
        return std::nullopt;
    }

    return topicLower.substr(0U, separatorIndex);
}

bool Rs485InterfaceComponent::valuesMatchForMatcher(const Value& left, const Value& right) {
    if (left == right) {
        return true;
    }

    const auto parseNumber = [](const Value& value) -> std::optional<double> {
        if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
            return *numericValue;
        }

        const auto* textValue = std::get_if<std::string>(&value);
        if (textValue == nullptr || textValue->empty()) {
            return std::nullopt;
        }

        std::size_t consumed = 0U;
        double parsed = 0.0;
        try {
            parsed = std::stod(*textValue, &consumed);
        } catch (...) {
            return std::nullopt;
        }
        if (consumed != textValue->size()) {
            return std::nullopt;
        }

        return parsed;
    };

    const auto leftNumber = parseNumber(left);
    const auto rightNumber = parseNumber(right);
    if (!leftNumber.has_value() || !rightNumber.has_value()) {
        return false;
    }

    return *leftNumber == *rightNumber;
}

std::optional<std::int64_t> Rs485InterfaceComponent::parseIsoTimestampMilliseconds(const std::string& timestamp) {
    constexpr std::int64_t k_milliseconds_per_second{1000LL};

    std::tm utcTime{};
    std::istringstream parser{timestamp};
    parser >> std::get_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    if (parser.fail()) {
        return std::nullopt;
    }

#if defined(_WIN32)
    const std::time_t epochSeconds = _mkgmtime(&utcTime);
#else
    const std::time_t epochSeconds = timegm(&utcTime);
#endif
    if (epochSeconds < 0) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(epochSeconds) * k_milliseconds_per_second;
}

void Rs485InterfaceComponent::addReasonsPreservingOrder(Message& target, const ReasonList& source) {
    for (const auto& reasonEntry : std::ranges::reverse_view(source)) {
        target.addReason(reasonEntry.message, reasonEntry.timestamp);
    }
}

std::string Rs485InterfaceComponent::deriveWildcardStartTopic(const std::string& addressTopic) {
    std::vector<std::string> chunks{};
    std::string current{};

    for (const char characterValue : addressTopic) {
        if (characterValue == '/') {
            chunks.push_back(current);
            current.clear();
        } else {
            current.push_back(characterValue);
        }
    }
    chunks.push_back(current);

    for (auto& chunk : chunks) {
        if (!chunk.empty()) {
            chunk = "+";
        }
    }

    std::string topic{};
    for (std::size_t index = 0U; index < chunks.size(); ++index) {
        if (index > 0U) {
            topic.push_back('/');
        }
        topic += chunks[index];
    }

    if (!topic.ends_with('/')) {
        topic.push_back('/');
    }

    return topic;
}

std::optional<std::uint32_t> Rs485InterfaceComponent::parsePositiveInteger(const Value& value) {
    if (const auto* number = std::get_if<double>(&value); number != nullptr) {
        const double rounded = std::round(*number);
        if (std::fabs(*number - rounded) > k_integer_epsilon || rounded < 0.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(rounded);
    }

    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) {
        return std::nullopt;
    }

    std::size_t consumed = 0U;
    const auto parsed = std::stoul(*text, &consumed);
    if (consumed != text->size()) {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(parsed);
}

void Rs485InterfaceComponent::processActionMessage(const Message& message) {
    const std::string topicLower = toLowerCopy(message.topic());

    if (endsWith(topicLower, "/set")) {
        const std::string topic = removeSuffix(topicLower, "/set");
        Message actionMessage{topic, message.value(), message.qos(), message.retain(), message.dup()};
        addReasonsPreservingOrder(actionMessage, message.reason());
        enqueueSet(actionMessage);
        return;
    }

    if (endsWith(topicLower, "/temporary")) {
        const std::string topic = removeSuffix(topicLower, "/temporary");
        Message actionMessage{topic, message.value(), message.qos(), message.retain(), message.dup()};
        addReasonsPreservingOrder(actionMessage, message.reason());
        enqueueTemporary(actionMessage);
        return;
    }

    // Preserve legacy quirk: check only for terminal "blink", not strictly "/blink".
    if (endsWith(topicLower, "blink")) {
        const std::string topic = removeSuffix(topicLower, "/blink");
        Message actionMessage{topic, message.value(), message.qos(), message.retain(), message.dup()};
        addReasonsPreservingOrder(actionMessage, message.reason());
        enqueueBlink(actionMessage);
        return;
    }

    (void)topicLower;
}

void Rs485InterfaceComponent::enqueueSet(const Message& actionMessage) {
    const Rs485MappedSerialData serialData = mapper_.toSerialData(actionMessage);
    Rs485SerialMessage serialMessage{};
    serialMessage.sender = config_.myAddress;
    serialMessage.receiver = serialData.address;
    serialMessage.command = serialData.command;
    serialMessage.value = static_cast<double>(serialData.value);
    serialMessage.reply = true;
    scheduler_.sendMessage(serialMessage);
}

void Rs485InterfaceComponent::enqueueTemporary(const Message& actionMessage) {
    std::uint32_t temporarySeconds = config_.temporaryOnSeconds;
    const auto parsedValue = parsePositiveInteger(actionMessage.value());
    if (parsedValue.has_value() && *parsedValue > 0U) {
        temporarySeconds = *parsedValue;
    }

    launchActionThread([this, actionMessage, temporarySeconds]() {
        Message onMessage{actionMessage.topic(), std::string{"on"}, actionMessage.qos(), actionMessage.retain(), actionMessage.dup()};
        addReasonsPreservingOrder(onMessage, actionMessage.reason());
        enqueueSet(onMessage);
        sleepInterruptible(running_, std::chrono::seconds{temporarySeconds});
        if (!running_) {
            return;
        }
        Message offMessage{actionMessage.topic(), std::string{"off"}, actionMessage.qos(), actionMessage.retain(), actionMessage.dup()};
        addReasonsPreservingOrder(offMessage, actionMessage.reason());
        enqueueSet(offMessage);
    });
}

void Rs485InterfaceComponent::enqueueBlink(const Message& actionMessage) {
    std::uint32_t amount = k_default_blink_cycles;
    const auto parsedAmount = parsePositiveInteger(actionMessage.value());
    if (parsedAmount.has_value() && *parsedAmount > 0U) {
        amount = *parsedAmount;
    }

    const std::uint32_t toggleCount = amount * k_blink_toggle_multiplier;

    launchActionThread([this, actionMessage, toggleCount]() {
        std::string cachedState = readCachedTopicState(actionMessage.topic());
        for (std::uint32_t index = 0U; index < toggleCount && running_; ++index) {
            Value toggled = toggledValueFromState(cachedState);
            Message toggledMessage{actionMessage.topic(), toggled, actionMessage.qos(), actionMessage.retain(), actionMessage.dup()};
            addReasonsPreservingOrder(toggledMessage, actionMessage.reason());
            enqueueSet(toggledMessage);
            cachedState = std::holds_alternative<std::string>(toggled)
                ? std::get<std::string>(toggled)
                : "off";

            sleepInterruptible(running_, std::chrono::seconds{config_.blinkDelaySeconds});
        }
    });
}

void Rs485InterfaceComponent::launchActionThread(std::function<void()> job) {
    std::thread worker{[job = std::move(job)]() {
        try {
            job();
        } catch (const std::exception& exceptionValue) {
            std::cout << "rs485_interface[action_error] worker exception " << exceptionValue.what() << '\n' << std::flush;
        } catch (...) {
            std::cout << "rs485_interface[action_error] worker exception unknown" << '\n' << std::flush;
        }
    }};

    std::lock_guard<std::mutex> lock{actionThreadsMutex_};
    actionThreads_.push_back(std::move(worker));
}

void Rs485InterfaceComponent::runSchedulerLoop() {
    while (running_) {
        scheduler_.processTick();
        sleepInterruptible(running_, std::chrono::milliseconds{config_.tickDelayMs});
    }
}

void Rs485InterfaceComponent::runTimeOfDayLoop() {
    while (running_) {
        Rs485SerialMessage message{};
        message.sender = config_.myAddress;
        message.reply = false;
        message.receiver = k_rs485_broadcast_address;
        message.command = 'C';

        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        const std::tm* localTime = std::localtime(&nowTime);
        const int totalMinutes = (localTime != nullptr)
            ? ((localTime->tm_hour * 60) + localTime->tm_min)
            : 0;
        message.value = static_cast<double>(totalMinutes);

        scheduler_.sendMessage(message);
        sleepInterruptible(running_, std::chrono::seconds{config_.timeOfDayDelaySeconds});
    }
}

void Rs485InterfaceComponent::onSchedulerSend(const Rs485SerialMessage& message) {
    std::vector<std::uint8_t> bytes{};
    try {
        bytes = encodeRs485SerialMessage(message);
    } catch (const std::exception& exceptionValue) {
        if (shouldTraceError(config_.traceLevel)) {
            std::cout << exceptionValue.what() << '\n' << std::flush;
        }
        return;
    }

    if (shouldTraceMessage(config_.traceLevel, message.isInternal())) {
        try {
            std::cout << buildLegacyLoggingInfo(message) << '\n' << std::flush;
        } catch (const std::exception& exceptionValue) {
            if (shouldTraceError(config_.traceLevel)) {
                std::cout << exceptionValue.what() << '\n' << std::flush;
            }
        }
    }

    std::lock_guard<std::mutex> lock{serialSendMutex_};
    if (static_cast<bool>(serialSendCallback_)) {
        serialSendCallback_(bytes);
    }
}

void Rs485InterfaceComponent::publishMappedMessages(const std::vector<Message>& messages) {
    std::lock_guard<std::mutex> lock{publishMutex_};
    if (!static_cast<bool>(publishCallback_)) {
        return;
    }

    for (const auto& message : messages) {
        Message publishMessage{message.topic(), message.value(), config_.subscribeQos, false, false};
        addReasonsPreservingOrder(publishMessage, message.reason());

        (void)publishCallback_(publishMessage);
        logOutgoingMessageIfEnabled(publishMessage);
    }
}

void Rs485InterfaceComponent::addReceivedMessage(const Message& message) {
    const auto replyTopic = deriveReplyTopicForMatcher(message.topic());
    if (!replyTopic.has_value()) {
        return;
    }

    std::lock_guard<std::mutex> lock{matcherMutex_};
    matchedRequests_[*replyTopic] = MatchedRequest{
        .value = message.value(),
        .reason = message.reason()};
}

void Rs485InterfaceComponent::matchAndUpdateReplyMessage(Message& message) {
    constexpr std::int64_t k_max_match_timespan_ms{30000LL};

    const std::string topicLower = toLowerCopy(message.topic());
    std::optional<MatchedRequest> matchedRequest{};
    {
        std::lock_guard<std::mutex> lock{matcherMutex_};
        const auto iterator = matchedRequests_.find(topicLower);
        if (iterator == matchedRequests_.end()) {
            return;
        }

        matchedRequest = iterator->second;
        matchedRequests_.erase(iterator);
    }

    if (!matchedRequest.has_value()) {
        return;
    }
    if (!valuesMatchForMatcher(matchedRequest->value, message.value())) {
        return;
    }
    if (matchedRequest->reason.empty() || message.reason().empty()) {
        return;
    }

    const auto receivedTimestamp = parseIsoTimestampMilliseconds(matchedRequest->reason.front().timestamp);
    const auto replyTimestamp = parseIsoTimestampMilliseconds(message.reason().front().timestamp);
    if (!receivedTimestamp.has_value() || !replyTimestamp.has_value()) {
        return;
    }

    const std::int64_t delta = *replyTimestamp - *receivedTimestamp;
    if (delta < 0LL || delta > k_max_match_timespan_ms) {
        return;
    }

    Message mergedMessage{message.topic(), message.value(), message.qos(), message.retain(), message.dup()};
    addReasonsPreservingOrder(mergedMessage, message.reason());
    addReasonsPreservingOrder(mergedMessage, matchedRequest->reason);
    message = std::move(mergedMessage);
}

std::string Rs485InterfaceComponent::readCachedTopicState(const std::string& topic) const {
    std::lock_guard<std::mutex> lock{topicStateMutex_};
    const auto iterator = topicStateCache_.find(topic);
    if (iterator == topicStateCache_.end()) {
        return "off";
    }
    return iterator->second;
}

void Rs485InterfaceComponent::updateTopicStateCache(const Message& message) {
    std::string stateText{"off"};
    if (const auto* valueText = std::get_if<std::string>(&message.value()); valueText != nullptr) {
        stateText = toLowerCopy(*valueText);
    } else if (std::fabs(std::get<double>(message.value()) - 1.0) < k_integer_epsilon) {
        stateText = "on";
    }

    std::lock_guard<std::mutex> lock{topicStateMutex_};
    topicStateCache_[toLowerCopy(message.topic())] = stateText;
}

void Rs485InterfaceComponent::logIncomingMessageIfEnabled(const Message& message) const {
    const MessageLogConfig logConfig{
        .enableIncoming = config_.logIncomingMessages,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };

    const std::optional<std::string> logLine = buildMessageLogLine(
        "rs485_interface",
        MessageLogDirection::Incoming,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void Rs485InterfaceComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = config_.logOutgoingMessages,
        .includeReasonChain = true,
    };

    const std::optional<std::string> logLine = buildMessageLogLine(
        "rs485_interface",
        MessageLogDirection::Outgoing,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

} // namespace yaha
