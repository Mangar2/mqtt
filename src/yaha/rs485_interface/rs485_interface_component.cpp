#include "yaha/rs485_interface/rs485_interface_component.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <thread>
#include <utility>

namespace yaha {
namespace {

constexpr std::uint32_t k_default_blink_cycles{1U};
constexpr std::uint32_t k_blink_toggle_multiplier{2U};
constexpr double k_integer_epsilon{1e-9};
constexpr const char* k_trace_topic_set{"$SYS/rs485Interface/trace/set"};
constexpr const char* k_monitor_trace_topic_set{"$MONITOR/rs485Interface/trace/set"};

[[nodiscard]] Value toggledValueFromState(const std::string& cachedState) {
    if (cachedState == "on") {
        return std::string{"off"};
    }
    return std::string{"on"};
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
            subscriptions[startTopic + settingSuffix + "/set"] = config_.subscribeQos;
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
    const std::string topicLower = toLowerCopy(message.topic());
    if (topicLower == toLowerCopy(k_trace_topic_set) || topicLower == toLowerCopy(k_monitor_trace_topic_set)) {
        if (std::holds_alternative<std::string>(message.value())) {
            config_.traceLevel = std::get<std::string>(message.value());
        }
        return;
    }

    processActionMessage(message);
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
}

void Rs485InterfaceComponent::close() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

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
            if (config_.traceLevel == "error" || config_.traceLevel == "messages" ||
                config_.traceLevel == "internal") {
                std::cout << "rs485_interface[decode_error] " << readResult.error << '\n';
            }
            continue;
        }

        const Rs485SerialMessage& serialMessage = *readResult.message;
        const bool sendToBroker = scheduler_.processReceivedMessage(serialMessage);
        if (!sendToBroker) {
            continue;
        }

        try {
            auto mappedMessages = mapper_.toMqttMessages(serialMessage);
            for (auto& mappedMessage : mappedMessages) {
                mappedMessage.addReason("received from arduino");
                updateTopicStateCache(mappedMessage);
            }
            publishMappedMessages(mappedMessages);
        } catch (const std::exception& exceptionValue) {
            if (config_.traceLevel == "error" || config_.traceLevel == "messages" ||
                config_.traceLevel == "internal") {
                std::cout << "rs485_interface[map_error] " << exceptionValue.what() << '\n';
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

bool Rs485InterfaceComponent::tryParsePositiveInteger(const Value& value, std::uint32_t& output) {
    if (const auto* number = std::get_if<double>(&value); number != nullptr) {
        const double rounded = std::round(*number);
        if (std::fabs(*number - rounded) > k_integer_epsilon || rounded < 0.0) {
            return false;
        }
        output = static_cast<std::uint32_t>(rounded);
        return true;
    }

    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) {
        return false;
    }

    std::size_t consumed = 0U;
    const auto parsed = std::stoul(*text, &consumed);
    if (consumed != text->size()) {
        return false;
    }

    output = static_cast<std::uint32_t>(parsed);
    return true;
}

void Rs485InterfaceComponent::processActionMessage(const Message& message) {
    const std::string topicLower = toLowerCopy(message.topic());

    if (endsWith(topicLower, "/set")) {
        const std::string topic = removeSuffix(topicLower, "/set");
        enqueueSet(topic, message.value());
        return;
    }

    if (endsWith(topicLower, "/temporary")) {
        const std::string topic = removeSuffix(topicLower, "/temporary");
        enqueueTemporary(topic, message.value());
        return;
    }

    // Preserve legacy quirk: check only for terminal "blink", not strictly "/blink".
    if (endsWith(topicLower, "blink")) {
        const std::string topic = removeSuffix(topicLower, "/blink");
        enqueueBlink(topic, message.value());
        return;
    }

    if (config_.traceLevel == "error" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
        std::cout << "rs485_interface[action_error] topic with unknown string end (/set, /temporary or /blink expected) "
                  << topicLower << '\n';
    }
}

void Rs485InterfaceComponent::enqueueSet(const std::string& topic, const Value& value) {
    Message actionMessage{topic, value};
    actionMessage.addReason("received by RS485Interface service");

    const Rs485MappedSerialData serialData = mapper_.toSerialData(actionMessage);
    Rs485SerialMessage serialMessage{};
    serialMessage.sender = config_.myAddress;
    serialMessage.receiver = serialData.address;
    serialMessage.command = serialData.command;
    serialMessage.value = static_cast<double>(serialData.value);
    serialMessage.reply = true;
    scheduler_.sendMessage(serialMessage);
}

void Rs485InterfaceComponent::enqueueTemporary(const std::string& topic, const Value& value) {
    std::uint32_t temporarySeconds = config_.temporaryOnSeconds;
    std::uint32_t parsedValue = 0U;
    if (tryParsePositiveInteger(value, parsedValue) && parsedValue > 0U) {
        temporarySeconds = parsedValue;
    }

    launchActionThread([this, topic, temporarySeconds]() {
        enqueueSet(topic, std::string{"on"});
        std::this_thread::sleep_for(std::chrono::seconds{temporarySeconds});
        if (!running_) {
            return;
        }
        enqueueSet(topic, std::string{"off"});
    });
}

void Rs485InterfaceComponent::enqueueBlink(const std::string& topic, const Value& value) {
    std::uint32_t amount = k_default_blink_cycles;
    std::uint32_t parsedAmount = 0U;
    if (tryParsePositiveInteger(value, parsedAmount) && parsedAmount > 0U) {
        amount = parsedAmount;
    }

    const std::uint32_t toggleCount = amount * k_blink_toggle_multiplier;

    launchActionThread([this, topic, toggleCount]() {
        std::string cachedState = readCachedTopicState(topic);
        for (std::uint32_t index = 0U; index < toggleCount && running_; ++index) {
            Value toggled = toggledValueFromState(cachedState);
            enqueueSet(topic, toggled);
            cachedState = std::holds_alternative<std::string>(toggled)
                ? std::get<std::string>(toggled)
                : "off";

            std::this_thread::sleep_for(std::chrono::seconds{config_.blinkDelaySeconds});
        }
    });
}

void Rs485InterfaceComponent::launchActionThread(std::function<void()> job) {
    std::thread worker{[job = std::move(job)]() {
        job();
    }};

    std::lock_guard<std::mutex> lock{actionThreadsMutex_};
    actionThreads_.push_back(std::move(worker));
}

void Rs485InterfaceComponent::runSchedulerLoop() {
    while (running_) {
        scheduler_.processTick();
        std::this_thread::sleep_for(std::chrono::milliseconds{config_.tickDelayMs});
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
        std::this_thread::sleep_for(std::chrono::seconds{config_.timeOfDayDelaySeconds});
    }
}

void Rs485InterfaceComponent::onSchedulerSend(const Rs485SerialMessage& message) {
    std::vector<std::uint8_t> bytes{};
    try {
        bytes = encodeRs485SerialMessage(message);
    } catch (const std::exception& exceptionValue) {
        if (config_.traceLevel == "error" || config_.traceLevel == "messages" ||
            config_.traceLevel == "internal") {
            std::cout << "rs485_interface[encode_error] " << exceptionValue.what() << '\n';
        }
        return;
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
        for (const auto& reasonEntry : message.reason()) {
            publishMessage.addReason(reasonEntry.message, reasonEntry.timestamp);
        }
        (void)publishCallback_(publishMessage);
    }
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

} // namespace yaha
