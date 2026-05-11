#include "yaha/zwave/zwave_service_component.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::int64_t kMaxReplyMatchTimespanMs = 30000;
constexpr double kNumericValueTolerance = 1e-9;
constexpr std::int64_t kMillisecondsPerSecond = 1000;

[[nodiscard]] bool isActionSegment(const std::string& segment) {
    static const std::array<std::string, 4> actionSegments{"set", "get", "temporary", "blink"};
    return std::find(actionSegments.begin(), actionSegments.end(), segment) != actionSegments.end();
}

[[nodiscard]] bool valuesMatch(const Value& left, const Value& right) {
    if (left.index() == right.index()) {
        if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) == std::get<std::string>(right);
        }
        return std::fabs(std::get<double>(left) - std::get<double>(right)) < kNumericValueTolerance;
    }

    if (std::holds_alternative<std::string>(left) && std::holds_alternative<double>(right)) {
        std::size_t consumedChars = 0U;
        const std::string& textValue = std::get<std::string>(left);
        const double parsed = std::stod(textValue, &consumedChars);
        return consumedChars == textValue.size()
            && std::fabs(parsed - std::get<double>(right)) < kNumericValueTolerance;
    }

    if (std::holds_alternative<double>(left) && std::holds_alternative<std::string>(right)) {
        return valuesMatch(right, left);
    }

    return false;
}

[[nodiscard]] std::optional<std::int64_t> parseIsoUtcTimestampMs(const std::string& timestamp) {
    std::tm parsed{};
    std::istringstream stream{timestamp};
    stream >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%SZ");
    if (stream.fail()) {
        return std::nullopt;
    }

#if defined(__APPLE__) || defined(__linux__)
    const std::time_t epochTime = timegm(&parsed);
#else
    const std::time_t epochTime = _mkgmtime(&parsed);
#endif
    if (epochTime < 0) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(epochTime) * kMillisecondsPerSecond;
}

[[nodiscard]] bool withinMatchTimespan(
    const Message& receivedMessage,
    const Message& replyMessage,
    const std::int64_t maxTimespanMs) {
    if (receivedMessage.reason().empty() || replyMessage.reason().empty()) {
        return false;
    }

    const auto receivedTimeMs = parseIsoUtcTimestampMs(receivedMessage.reason().front().timestamp);
    const auto replyTimeMs = parseIsoUtcTimestampMs(replyMessage.reason().front().timestamp);
    if (!receivedTimeMs.has_value() || !replyTimeMs.has_value()) {
        return false;
    }

    const std::int64_t deltaMs = *replyTimeMs - *receivedTimeMs;
    return deltaMs >= 0 && deltaMs <= maxTimespanMs;
}

[[nodiscard]] Message rebuildMessageWithReasons(const Message& source, const std::vector<ReasonEntry>& reasons) {
    Message rebuilt{source.topic(), source.value(), source.qos(), source.retain(), source.dup()};
    for (auto iterator = reasons.rbegin(); iterator != reasons.rend(); ++iterator) {
        rebuilt.addReason(iterator->message, iterator->timestamp);
    }

    if (source.rawPayload().has_value()) {
        rebuilt.setRawPayload(*source.rawPayload());
    }

    return rebuilt;
}

class ReplyMatcher {
public:
    void addReceivedMessage(const Message& message) {
        const auto splitResult = splitActionTopic(message.topic());
        if (!splitResult.has_value()) {
            return;
        }

        receivedByReplyTopic_.insert_or_assign(splitResult->first, message);
    }

    [[nodiscard]] Message matchAndUpdateReplyMessage(const Message& outgoingMessage) {
        const auto iterator = receivedByReplyTopic_.find(outgoingMessage.topic());
        const bool hasStoredMessage = iterator != receivedByReplyTopic_.end();
        if (!hasStoredMessage) {
            return outgoingMessage;
        }

        const Message receivedMessage = iterator->second;
        receivedByReplyTopic_.erase(iterator);

        const bool matchingValue = valuesMatch(receivedMessage.value(), outgoingMessage.value());
        const bool matchingTime = withinMatchTimespan(receivedMessage, outgoingMessage, kMaxReplyMatchTimespanMs);
        if (!matchingValue || !matchingTime) {
            return outgoingMessage;
        }

        std::vector<ReasonEntry> mergedReasons{};
        mergedReasons.reserve(receivedMessage.reason().size() + outgoingMessage.reason().size());
        mergedReasons.insert(mergedReasons.end(), receivedMessage.reason().begin(), receivedMessage.reason().end());
        mergedReasons.insert(mergedReasons.end(), outgoingMessage.reason().begin(), outgoingMessage.reason().end());
        return rebuildMessageWithReasons(outgoingMessage, mergedReasons);
    }

private:
    [[nodiscard]] static std::optional<std::pair<std::string, std::string>> splitActionTopic(const std::string& topic) {
        const std::size_t separatorPos = topic.rfind('/');
        if (separatorPos == std::string::npos || separatorPos + 1U >= topic.size()) {
            return std::nullopt;
        }

        const std::string actionSegment = topic.substr(separatorPos + 1U);
        if (!isActionSegment(actionSegment)) {
            return std::nullopt;
        }

        const std::string replyTopic = topic.substr(0U, separatorPos);
        return std::make_pair(replyTopic, actionSegment);
    }

    std::unordered_map<std::string, Message> receivedByReplyTopic_{};
};

[[nodiscard]] Message withPublishFlags(const Message& input, const Qos qos, const bool retain) {
    Message output{input.topic(), input.value(), qos, retain, input.dup()};
    for (auto iterator = input.reason().rbegin(); iterator != input.reason().rend(); ++iterator) {
        output.addReason(iterator->message, iterator->timestamp);
    }
    if (input.rawPayload().has_value()) {
        output.setRawPayload(*input.rawPayload());
    }
    return output;
}

ReplyMatcher& sharedReplyMatcher() {
    static ReplyMatcher matcher{};
    return matcher;
}

} // namespace

ZwaveServiceComponent::ZwaveServiceComponent(ZwaveConfig config, std::shared_ptr<IZwaveController> controller)
    : config_(std::move(config))
    , controller_(std::move(controller)) {
    if (controller_ == nullptr) {
        throw std::invalid_argument("zwave controller must not be null");
    }

    controller_->setDeviceConfiguration(config_.devices);
    controller_->setPublishCallback([this](const Message& message) {
        handleControllerPublish(message);
    });
}

void ZwaveServiceComponent::setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& config) {
    controller_->setDeviceConfiguration(config);

    Message infoMessage{"$MONITORING/zwave/info", std::string{"configuration reloaded"}};
    infoMessage.addReason("updated");
    publish(withPublishFlags(infoMessage, config_.qos, config_.retain));
}

SubscriptionMap ZwaveServiceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({"$MONITORING/zwave/removefailednode/set", Qos::ExactlyOnce});
    subscriptions.insert({"$MONITORING/zwave/addnode/set", Qos::ExactlyOnce});
    subscriptions.insert({"$MONITORING/zwave/scan/set", Qos::ExactlyOnce});

    for (const auto& device : config_.devices) {
        std::string topic = device.topic;
        if (device.classId.has_value()) {
            topic += "/set";
        } else {
            topic += "/+/set";
        }
        subscriptions.insert({topic, config_.subscribeQos});
    }

    return subscriptions;
}

void ZwaveServiceComponent::handleMessage(const Message& message) {
    if (isRemoveFailedTopic(message.topic())) {
        controller_->removeFailedNode(message.value());
        return;
    }

    if (isAddNodeTopic(message.topic())) {
        controller_->addDevice();
        return;
    }

    if (isScanTopic(message.topic())) {
        try {
            controller_->startScan();
            Message notification{"$MONITORING/zwave/notification", std::string{"scan command accepted"}};
            notification.addReason("scan command accepted by controller");
            publish(withPublishFlags(notification, config_.qos, config_.retain));
        } catch (const std::exception& exception) {
            Message error{"$MONITORING/zwave/error", std::string{"scan command failed"}};
            error.addReason(exception.what());
            publish(withPublishFlags(error, config_.qos, config_.retain));
        }
        return;
    }

    Message routedMessage{message.topic(), message.value(), message.qos(), message.retain(), message.dup()};
    for (auto iterator = message.reason().rbegin(); iterator != message.reason().rend(); ++iterator) {
        routedMessage.addReason(iterator->message, iterator->timestamp);
    }
    routedMessage.addReason("received by zwave service");

    sharedReplyMatcher().addReceivedMessage(routedMessage);
    controller_->setValue(message.topic(), message.value());
}

void ZwaveServiceComponent::run() {
    Message removeFailedRestart{"$MONITORING/zwave/removefailednode", std::string{"nop"}};
    removeFailedRestart.addReason("zwave restarted");
    publish(withPublishFlags(removeFailedRestart, config_.qos, config_.retain));

    Message addNodeRestart{"$MONITORING/zwave/addnode", std::string{"nop"}};
    addNodeRestart.addReason("zwave restarted");
    publish(withPublishFlags(addNodeRestart, config_.qos, config_.retain));

    controller_->requestConfigParametersForAllNodes();
}

void ZwaveServiceComponent::close() {
    controller_->close();
}

void ZwaveServiceComponent::setPublishCallback(PublishCallback callback) {
    publishCallback_ = std::move(callback);
}

void ZwaveServiceComponent::handleControllerPublish(const Message& message) {
    const Message matched = sharedReplyMatcher().matchAndUpdateReplyMessage(message);
    publish(withPublishFlags(matched, config_.qos, config_.retain));
}

void ZwaveServiceComponent::publish(const Message& message) const {
    if (publishCallback_) {
        publishCallback_(message);
    }
}

bool ZwaveServiceComponent::isRemoveFailedTopic(const std::string& topic) {
    return topic == "$MONITORING/zwave/removefailednode/set";
}

bool ZwaveServiceComponent::isAddNodeTopic(const std::string& topic) {
    return topic == "$MONITORING/zwave/addnode/set";
}

bool ZwaveServiceComponent::isScanTopic(const std::string& topic) {
    return topic == "$MONITORING/zwave/scan/set";
}

} // namespace yaha
