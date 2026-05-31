#include "yaha/serial_device/serial_device_component.h"

#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/serial_device/serial_device_mqtt_to_serial_mapper.h"
#include "yaha/serial_device/serial_device_serial_to_mqtt_mapper.h"
#include "yaha/serial_device/serial_device_wire_serializer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

namespace yaha {
namespace {

constexpr std::uint32_t k_send_retry_count{3U};
constexpr std::uint32_t k_open_retry_count{10U};
constexpr std::chrono::milliseconds k_open_retry_delay{15000};
constexpr std::chrono::milliseconds k_send_queue_spacing{100};
constexpr double k_integer_epsilon{1e-9};

[[nodiscard]] bool valueEquals(const Value& leftValue, const Value& rightValue) {
    if (leftValue.index() != rightValue.index()) {
        return false;
    }

    if (std::holds_alternative<std::string>(leftValue)) {
        return std::get<std::string>(leftValue) == std::get<std::string>(rightValue);
    }

    return std::fabs(std::get<double>(leftValue) - std::get<double>(rightValue)) <= k_integer_epsilon;
}

[[nodiscard]] Message cloneWithQos(const Message& sourceMessage, const Qos qosValue, std::string topicValue) {
    Message outputMessage{
        std::move(topicValue),
        sourceMessage.value(),
        qosValue,
        sourceMessage.retain(),
        sourceMessage.dup()};

    for (const auto& reasonValue : sourceMessage.reason() | std::views::reverse) {
        outputMessage.addReason(reasonValue.message, reasonValue.timestamp);
    }

    return outputMessage;
}

} // namespace

ISerialDeviceTransport::~ISerialDeviceTransport() = default;

SerialDeviceComponent::SerialDeviceComponent(
    SerialDeviceConfig configValue,
    std::shared_ptr<ISerialDeviceTransport> transportValue,
    DelayFunction delayFunction)
    : config_(std::move(configValue))
    , transport_(std::move(transportValue))
    , delayFunction_(std::move(delayFunction)) {
    if (!delayFunction_) {
        delayFunction_ = [](const std::chrono::milliseconds durationValue) {
            std::this_thread::sleep_for(durationValue);
        };
    }
}

SerialDeviceComponent::~SerialDeviceComponent() {
    close();
}

SubscriptionMap SerialDeviceComponent::getSubscriptions() const {
    return deriveSerialDeviceSubscriptions(config_);
}

void SerialDeviceComponent::handleMessage(const Message& messageValue) {
    try {
        logIncomingMessageIfEnabled(messageValue);

        if (messageValue.topic() == "$SYS/serialdevice/trace/set") {
            if (std::holds_alternative<std::string>(messageValue.value())) {
                std::lock_guard<std::mutex> stateLock{stateMutex_};
                config_.traceLevel = std::get<std::string>(messageValue.value());
            }
            return;
        }

        Message receivedMessage{messageValue.topic(), messageValue.value(), messageValue.qos(), messageValue.retain(), messageValue.dup()};
        for (const auto& reasonValue : messageValue.reason() | std::views::reverse) {
            receivedMessage.addReason(reasonValue.message, reasonValue.timestamp);
        }
        receivedMessage.addReason("received by serialDevice interface service", "");
        addReceivedMessage(receivedMessage);

        const std::string normalizedTopic = stripSetSuffix(messageValue.topic());
        const SerialDeviceMessage serialMessage = mapMqttToSerialMessage(config_, normalizedTopic, valueToText(messageValue.value()));
        const std::string payloadText = serialDeviceMessageToWireString(serialMessage);
        enqueueSendPayload(payloadText);
    } catch (const std::exception& exceptionValue) {
        if (config_.traceLevel == "errors" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
            std::cout << exceptionValue.what() << '\n' << std::flush;
        }
    }
}

void SerialDeviceComponent::run() {
    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (isRunning_) {
            return;
        }
        isRunning_ = true;
        isClosed_ = false;
    }

    transport_->setReceiveCallback([this](const std::vector<std::uint8_t>& chunkBytes) {
        processReceivedSerialData(chunkBytes);
    });

    try {
        openSerialInterface();
        std::cout << "Serial service is running" << '\n' << std::flush;
    } catch (const std::exception& exceptionValue) {
        if (config_.traceLevel == "errors" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
            std::cout << exceptionValue.what() << '\n' << std::flush;
        }
        close();
        return;
    }

    sendQueueThread_ = std::thread([this]() {
        runSendQueueLoop();
    });

    keepAliveThread_ = std::thread([this]() {
        runSendKeepAliveLoop();
    });
}

void SerialDeviceComponent::close() {
    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (!isRunning_ && isClosed_) {
            return;
        }
        isClosed_ = true;
        isRunning_ = false;
    }

    queueCondition_.notify_all();

    if (sendQueueThread_.joinable()) {
        sendQueueThread_.join();
    }

    if (keepAliveThread_.joinable()) {
        keepAliveThread_.join();
    }

    try {
        transport_->close();
    } catch (const std::exception&) {
    }

    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        isSerialInterfaceOpen_ = false;
    }

    std::cout << "serial device service closed" << '\n' << std::flush;
}

void SerialDeviceComponent::setPublishCallback(PublishCallback callbackValue) {
    std::lock_guard<std::mutex> publishLock{publishMutex_};
    publishCallback_ = std::move(callbackValue);
}

std::string SerialDeviceComponent::traceLevel() const {
    std::lock_guard<std::mutex> stateLock{stateMutex_};
    return config_.traceLevel;
}

bool SerialDeviceComponent::topicsMatchIgnoringSetSuffix(
    const std::string& leftTopic,
    const std::string& rightTopic) {
    return stripSetSuffix(leftTopic) == stripSetSuffix(rightTopic);
}

std::string SerialDeviceComponent::stripSetSuffix(const std::string& topicValue) {
    if (topicValue.size() >= 4U && topicValue.ends_with("/set")) {
        return topicValue.substr(0U, topicValue.size() - 4U);
    }
    return topicValue;
}

std::string SerialDeviceComponent::stripActionSuffix(const std::string& topicValue) {
    constexpr std::array<std::string_view, 4U> actionSuffixes{
        "/set",
        "/get",
        "/temporary",
        "/blink"};

    for (const auto actionSuffix : actionSuffixes) {
        if (topicValue.size() >= actionSuffix.size() && topicValue.ends_with(actionSuffix)) {
            return topicValue.substr(0U, topicValue.size() - actionSuffix.size());
        }
    }

    return topicValue;
}

bool SerialDeviceComponent::isActionTopic(const std::string& topicValue) {
    return stripActionSuffix(topicValue) != topicValue;
}

std::string SerialDeviceComponent::valueToText(const Value& valueValue) {
    if (std::holds_alternative<std::string>(valueValue)) {
        return std::get<std::string>(valueValue);
    }

    const double numericValue = std::get<double>(valueValue);
    const double roundedValue = std::round(numericValue);
    std::ostringstream output{};
    if (std::fabs(numericValue - roundedValue) <= k_integer_epsilon) {
        output << static_cast<std::int64_t>(roundedValue);
    } else {
        output << numericValue;
    }
    return output.str();
}

void SerialDeviceComponent::runSendKeepAliveLoop() {
    while (true) {
        {
            std::lock_guard<std::mutex> stateLock{stateMutex_};
            if (isClosed_) {
                break;
            }
        }

        try {
            sendDataToSerialWithRetry("at");
        } catch (const std::exception& exceptionValue) {
            if (config_.traceLevel == "errors" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
                std::cout << exceptionValue.what() << '\n' << std::flush;
            }
        }

        delayFor(std::chrono::seconds{config_.keepAliveDelayInSeconds});
    }
}

void SerialDeviceComponent::runSendQueueLoop() {
    while (true) {
        std::string payloadText{};
        if (!tryDequeueSendPayload(payloadText)) {
            break;
        }

        try {
            sendDataToSerialWithRetry(payloadText);
        } catch (const std::exception& exceptionValue) {
            if (config_.traceLevel == "errors" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
                std::cout << exceptionValue.what() << '\n' << std::flush;
            }
        }

        delayFor(k_send_queue_spacing);
    }
}

void SerialDeviceComponent::processReceivedSerialData(const std::vector<std::uint8_t>& chunkBytes) {
    const std::string chunkText{chunkBytes.begin(), chunkBytes.end()};

    if (const auto firstMessage = parser_.parseChunk(chunkText); firstMessage.has_value()) {
        processParsedSerialMessage(firstMessage.value());
    }

    while (true) {
        const auto nextMessage = parser_.parseChunk("");
        if (!nextMessage.has_value()) {
            break;
        }
        processParsedSerialMessage(nextMessage.value());
    }
}

void SerialDeviceComponent::processParsedSerialMessage(const SerialDeviceMessage& serialMessage) {
    try {
        const std::vector<Message> mqttMessages = mapSerialMessageToMqttMessages(config_, serialMessage);
        publishMessages(mqttMessages);
    } catch (const std::exception& exceptionValue) {
        if (config_.traceLevel == "errors" || config_.traceLevel == "messages" || config_.traceLevel == "internal") {
            std::cout << exceptionValue.what() << '\n' << std::flush;
        }
    }
}

void SerialDeviceComponent::publishMessages(const std::vector<Message>& mqttMessages) {
    PublishCallback callbackCopy{};
    {
        std::lock_guard<std::mutex> publishLock{publishMutex_};
        callbackCopy = publishCallback_;
    }

    for (const auto& sourceMessage : mqttMessages) {
        const bool isSetMessage = sourceMessage.topic().ends_with("/set");
        const std::string baseTopic = stripSetSuffix(sourceMessage.topic());

        Message publishMessage = cloneWithQos(sourceMessage, config_.subscribeQos, baseTopic);

        if (hasMatchingMessage(publishMessage)) {
            publishMessage = matchAndUpdateReplyMessage(publishMessage);
        } else if (isSetMessage) {
            publishMessage = cloneWithQos(sourceMessage, config_.subscribeQos, baseTopic + "/set");
        }

        if (!callbackCopy) {
            logOutgoingFailure(publishMessage, "callback_missing", "publish callback not set");
            continue;
        }

        try {
            const PublishResult publishResult = callbackCopy(publishMessage);
            if (!publishResult.success) {
                const std::string reasonText = publishResult.reason.empty()
                    ? "unspecified"
                    : publishResult.reason;
                logOutgoingFailure(
                    publishMessage,
                    "publish_failed",
                    reasonText);
                continue;
            }

            logOutgoingMessageIfEnabled(publishMessage);
        } catch (const std::exception& exceptionValue) {
            logOutgoingFailure(publishMessage, "publish_failed", exceptionValue.what());
        } catch (...) {
            logOutgoingFailure(publishMessage, "publish_failed", "unknown");
        }
    }
}

void SerialDeviceComponent::enqueueSendPayload(const std::string& payloadText) {
    {
        std::lock_guard<std::mutex> queueLock{queueMutex_};
        sendQueue_.push_back(payloadText);
    }
    queueCondition_.notify_one();
}

bool SerialDeviceComponent::tryDequeueSendPayload(std::string& payloadText) {
    std::unique_lock<std::mutex> queueLock{queueMutex_};
    queueCondition_.wait(queueLock, [this]() {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        return !sendQueue_.empty() || isClosed_;
    });

    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (sendQueue_.empty() && isClosed_) {
            return false;
        }
    }

    if (sendQueue_.empty()) {
        return false;
    }

    payloadText = sendQueue_.front();
    sendQueue_.pop_front();
    return true;
}

void SerialDeviceComponent::sendDataToSerialWithRetry(const std::string& serialString) {
    std::uint32_t retry = k_send_retry_count;
    while (retry > 0U) {
        try {
            transport_->sendData(serialString);
            retry = 0U;
        } catch (const std::exception&) {
            if (retry == 0U) {
                throw;
            }
            openSerialInterface();
            retry -= 1U;
        }
    }
}

void SerialDeviceComponent::openSerialInterface() {
    std::uint32_t loop = 1U;

    {
        std::lock_guard<std::mutex> stateLock{stateMutex_};
        if (isSerialInterfaceOpen_ && transport_->isOpen()) {
            transport_->close();
            isSerialInterfaceOpen_ = false;
        }
    }

    while (true) {
        try {
            transport_->open(config_.serialPortName, config_.baudrate);
            std::lock_guard<std::mutex> stateLock{stateMutex_};
            isSerialInterfaceOpen_ = true;
            return;
        } catch (const std::exception&) {
            transport_->listAvailablePorts();
            loop += 1U;
            if (loop >= k_open_retry_count) {
                throw;
            }
            delayFor(k_open_retry_delay);
        }
    }
}

void SerialDeviceComponent::delayFor(const std::chrono::milliseconds durationValue) const {
    if (delayFunction_) {
        delayFunction_(durationValue);
    }
}

bool SerialDeviceComponent::hasMatchingMessage(const Message& messageValue) const {
    std::lock_guard<std::mutex> matcherLock{matcherMutex_};
    return std::ranges::any_of(receivedMessages_, [&messageValue](const MatchedRequest& receivedMessage) {
        return receivedMessage.topic == messageValue.topic()
            && valueEquals(receivedMessage.value, messageValue.value());
    });
}

Message SerialDeviceComponent::matchAndUpdateReplyMessage(const Message& messageValue) {
    std::lock_guard<std::mutex> matcherLock{matcherMutex_};
    Message updatedMessage = messageValue.clone();

    for (auto iterator = receivedMessages_.begin(); iterator != receivedMessages_.end(); ++iterator) {
        if (iterator->topic != messageValue.topic()) {
            continue;
        }

        const bool valuesMatch = valueEquals(iterator->value, messageValue.value());
        const ReasonList matchedReason = iterator->reason;
        receivedMessages_.erase(iterator);

        if (valuesMatch) {
            ReasonList mergedReason = updatedMessage.reason();
            mergedReason.insert(mergedReason.end(), matchedReason.begin(), matchedReason.end());

            Message mergedMessage{
                updatedMessage.topic(),
                updatedMessage.value(),
                updatedMessage.qos(),
                updatedMessage.retain(),
                updatedMessage.dup()};
            for (const auto& reasonValue : mergedReason | std::views::reverse) {
                mergedMessage.addReason(reasonValue.message, reasonValue.timestamp);
            }
            updatedMessage = std::move(mergedMessage);
        }

        return updatedMessage;
    }

    return updatedMessage;
}

void SerialDeviceComponent::addReceivedMessage(const Message& messageValue) {
    if (!isActionTopic(messageValue.topic())) {
        return;
    }

    const std::string replyTopic = stripActionSuffix(messageValue.topic());

    std::lock_guard<std::mutex> matcherLock{matcherMutex_};
    std::erase_if(receivedMessages_, [&replyTopic](const MatchedRequest& requestValue) {
        return requestValue.topic == replyTopic;
    });

    receivedMessages_.push_back(MatchedRequest{
        .topic = replyTopic,
        .value = messageValue.value(),
        .reason = messageValue.reason()});

    constexpr std::size_t k_max_queue_size{128U};
    while (receivedMessages_.size() > k_max_queue_size) {
        receivedMessages_.pop_front();
    }
}

void SerialDeviceComponent::logIncomingMessageIfEnabled(const Message& messageValue) const {
    const MessageLogConfig logConfig{
        .enableIncoming = config_.logIncomingMessages,
        .enableOutgoing = false,
        .includeReasonChain = config_.logReason,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "serial_device",
        MessageLogDirection::Incoming,
        messageValue,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void SerialDeviceComponent::logOutgoingMessageIfEnabled(const Message& messageValue) const {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = config_.logOutgoingMessages,
        .includeReasonChain = config_.logReason,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "serial_device",
        MessageLogDirection::Outgoing,
        messageValue,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void SerialDeviceComponent::logOutgoingFailure(const Message& messageValue,
                                               const std::string& categoryText,
                                               const std::string& reasonText) {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const std::optional<std::string> logLine = buildMessageLogLine(
        "serial_device",
        MessageLogDirection::Outgoing,
        messageValue,
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
