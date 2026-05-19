#include "yaha/zwave_controller/zwave_controller.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <thread>
#include <unordered_set>
#include <utility>

namespace yaha {

namespace {

constexpr std::size_t kSetTopicMinimumParts = 2U;
constexpr std::uint16_t kUsbControllerNodeId = 1U;
constexpr double kIntegerTolerance = 1e-9;
constexpr std::uint32_t kPendingCommandLoopSleepMs = 20U;
constexpr bool kPendingPollingDebugTrace = true;

void logPendingPollingTrace(const std::string& text) {
    if (!kPendingPollingDebugTrace) {
        return;
    }
    std::cout << "zwave_controller[pending-trace] " << text << '\n' << std::flush;
}

[[nodiscard]] std::string valueToString(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text;
    }

    std::ostringstream stream{};
    stream << std::get<double>(value);
    return stream.str();
}

[[nodiscard]] bool valueAsBool(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text == "on" || *text == "1" || *text == "true";
    }
    return std::fabs(std::get<double>(value) - 1.0) < kIntegerTolerance;
}

[[nodiscard]] Value applySwitchOutboundConversion(const Value& value, const std::string& typeName) {
    if (typeName != "switch") {
        return value;
    }

    return valueAsBool(value) ? Value{std::string{"on"}} : Value{std::string{"off"}};
}

[[nodiscard]] std::optional<bool> valueAsSemanticBool(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        if (std::fabs(*numericValue - 1.0) < kIntegerTolerance) {
            return true;
        }
        if (std::fabs(*numericValue) < kIntegerTolerance) {
            return false;
        }
        return std::nullopt;
    }

    std::string normalized = std::get<std::string>(value);
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (normalized == "on" || normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "off" || normalized == "false" || normalized == "0") {
        return false;
    }

    return std::nullopt;
}

[[nodiscard]] double valueAsDouble(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        return *numericValue;
    }

    std::size_t parsedChars = 0U;
    const auto& text = std::get<std::string>(value);
    const double parsed = std::stod(text, &parsedChars);
    if (parsedChars != text.size()) {
        throw std::runtime_error("invalid numeric value '" + text + "'");
    }
    return parsed;
}

} // namespace

ZwaveController::ZwaveController(
    ZwaveUsbConfig usbConfig,
    IZwaveDriverPort& driverPort,
    const std::uint32_t commandReactionPollIntervalMs,
    const std::uint32_t commandReactionTimeoutMs)
    : usb_(std::move(usbConfig))
    , driverPort_(driverPort)
    , devicesMapper_(std::vector<ZwaveDeviceConfig>{})
    , commandReactionPollInterval_(commandReactionPollIntervalMs)
    , commandReactionTimeout_(commandReactionTimeoutMs) {
    pendingCommandPollThread_ = std::thread([this] {
        runPendingCommandPollLoop();
    });
}

ZwaveController::~ZwaveController() {
    if (pendingCommandPollThread_.joinable()) {
        pendingCommandPollStop_.store(true);
        pendingCommandPollThread_.join();
    }
}

void ZwaveController::setPublishCallback(PublishCallback callback) {
    publishCallback_ = std::move(callback);
}

void ZwaveController::setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& devices) {
    devices_ = devices;
    devicesMapper_ = ZwaveDevicesMapper{devices_};
}

void ZwaveController::setValue(const std::string& topic, const Value& value, const std::vector<ReasonEntry>& reasons) {
    const std::vector<std::string> topicParts = splitTopic(topic);
    if (topicParts.size() < kSetTopicMinimumParts) {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    if (topicParts.back() != "set") {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    const ZwaveNodeMap nodeMap = buildNodeMap();
    const std::string directTopic = joinTopicParts(topicParts, topicParts.size() - 1U);

    std::string replyTopic = directTopic;
    ZwaveResolvedId target{};
    try {
        target = devicesMapper_.topicToZwaveId(nodeMap, directTopic, std::nullopt);
    } catch (...) {
        const std::optional<std::string> objectLabel = parseOptionalLabelFromSetTopic(topicParts);
        const std::string deviceTopic = joinTopicParts(topicParts, topicParts.size() - kSetTopicMinimumParts);
        target = devicesMapper_.topicToZwaveId(nodeMap, deviceTopic, objectLabel);
        replyTopic = objectLabel.has_value() ? deviceTopic + "/" + *objectLabel : deviceTopic;
    }

    const ZwaveWriteRequest writeRequest = ZwaveDevicesMapper::buildWriteRequest(target, value);

    if (writeRequest.kind == ZwaveWriteKind::SetConfigParam) {
        driverPort_.setConfigParam(target.nodeId, target.index, std::get<double>(writeRequest.value));
        return;
    }

    driverPort_.setValue(target, writeRequest.value);
    rememberPendingCommand(replyTopic, writeRequest, reasons);
}

void ZwaveController::addDevice() {
    driverPort_.addNode();
}

void ZwaveController::removeFailedNode(const Value& value) {
    const std::optional<std::uint16_t> nodeId = parseNodeIdFromValue(value);
    if (!nodeId.has_value()) {
        throw std::runtime_error("removefailednode requires numeric node id");
    }
    driverPort_.removeFailedNode(*nodeId);
}

void ZwaveController::startScan() {
    driverPort_.startScan();
}

void ZwaveController::requestConfigParametersForAllNodes() {
    for (const auto& device : devices_) {
        driverPort_.requestAllConfigParams(device.nodeId);
    }
}

void ZwaveController::close() {
    driverPort_.disconnect(usb_.device);
}

void ZwaveController::onDriverReady(const std::uint32_t homeId) {
    std::ostringstream reason{};
    reason << "scanning homeid=0x" << std::hex << homeId;
    publish("$MONITOR/zwave/notification", std::string{"starting scan"}, reason.str());
}

void ZwaveController::onDriverFailed() {
    publish("$MONITOR/zwave/error", std::string{"driver failure"}, "failed to start driver. Stopping module");

    if (driverFailedCallback_) {
        driverFailedCallback_();
    }
}

void ZwaveController::setDriverFailedCallback(std::function<void()> callback) {
    driverFailedCallback_ = std::move(callback);
}

void ZwaveController::onScanComplete() {
    publish("$MONITOR/zwave/notification", std::string{"scan complete"}, "zwave info");
}

void ZwaveController::onNotification(const std::uint16_t nodeId, const ZwaveNotificationCode notification) {
    try {
        const ZwaveValueDescriptor descriptor{
            .nodeId = nodeId,
            .classId = 0U,
            .instance = 1U,
            .index = 0U,
            .label = std::nullopt,
            .valueId = std::nullopt};

        const std::optional<ZwaveTopicMapping> mapping = devicesMapper_.valueToTopicAndType(descriptor);
        std::string topic = "/$MONITOR/zwave/unknown node " + std::to_string(nodeId);
        if (mapping.has_value() && !mapping->topic.empty()) {
            topic = mapping->topic;
        }

        publish(topic, notificationText(notification), "zwave notification");
    } catch (...) {
        const std::string text = notificationText(notification);
        publish("$MONITOR/zwave/error", text, "node: " + std::to_string(nodeId) + " " + text);
    }
}

void ZwaveController::onControllerCommand(const std::int32_t resultCode, const std::string& statusText) {
    publish(
        "$MONITOR/zwave/notification",
        statusText,
        "controller commmand feedback: r=" + std::to_string(resultCode) + " s=" + statusText);
}

void ZwaveController::onNodeAdded(const std::uint16_t nodeId) {
    nodes_[nodeId] = NodeRuntimeState{};
}

void ZwaveController::onNodeReady(const std::uint16_t nodeId, const ZwaveNodeInfo& nodeInfo) {
    auto nodeIterator = nodes_.find(nodeId);
    if (nodeIterator == nodes_.end()) {
        nodeIterator = nodes_.insert({nodeId, NodeRuntimeState{}}).first;
    }

    nodeIterator->second.info = nodeInfo;
    nodeIterator->second.ready = true;
}

void ZwaveController::onValueAdded(const ZwaveControllerValueEvent& event) {
    storeNodeValue(event);
}

void ZwaveController::onValueRemoved(const std::uint16_t nodeId, const std::uint16_t classId, const std::uint8_t index) {
    const auto nodeIterator = nodes_.find(nodeId);
    if (nodeIterator == nodes_.end()) {
        return;
    }

    auto classIterator = nodeIterator->second.classes.find(classId);
    if (classIterator == nodeIterator->second.classes.end()) {
        return;
    }

    classIterator->second.erase(index);
}

void ZwaveController::onValueChanged(const ZwaveControllerValueEvent& event) {
    storeNodeValue(event);

    std::string reason = "received from zwave";
    if (event.valueId.has_value()) {
        reason += ", id: " + std::to_string(*event.valueId);
    }

    publishValue(event.nodeId, event, std::move(reason));
}

void ZwaveController::onValueRefreshed(
    const std::uint16_t nodeId,
    const std::uint16_t classId,
    const ZwaveControllerValueEvent& event) {
    (void)nodeId;
    (void)classId;
    storeNodeValue(event);

    try {
        if (event.nodeId == kUsbControllerNodeId) {
            return;
        }

        const std::optional<ZwaveTopicMapping> mapping = devicesMapper_.valueToTopicAndType(buildDescriptor(event));
        if (!mapping.has_value() || mapping->topic.empty()) {
            return;
        }

        const Value outboundValue = applySwitchOutboundConversion(event.value, mapping->type);
        PendingCommandMatch pendingMatch = takeMatchingPendingReasons(mapping->topic, event, outboundValue);
        if (!pendingMatch.matched) {
            return;
        }

        std::string reason = "received from zwave refresh";
        if (event.valueId.has_value()) {
            reason += ", id: " + std::to_string(*event.valueId);
        }

        publish(mapping->topic, outboundValue, reason, pendingMatch.reasons);
    } catch (...) {
    }
}

std::optional<std::uint16_t> ZwaveController::parseNodeIdFromValue(const Value& value) {
    const double numericValue = valueAsDouble(value);
    if (numericValue < 0.0 || numericValue > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }

    const double rounded = std::round(numericValue);
    if (std::fabs(rounded - numericValue) > kIntegerTolerance) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(rounded);
}

std::optional<std::string> ZwaveController::parseOptionalLabelFromSetTopic(
    const std::vector<std::string>& topicParts) {
    if (topicParts.size() <= kSetTopicMinimumParts) {
        return std::nullopt;
    }

    const std::string& label = topicParts[topicParts.size() - kSetTopicMinimumParts];
    if (label.empty()) {
        return std::nullopt;
    }
    return label;
}

std::string ZwaveController::joinTopicParts(const std::vector<std::string>& parts, const std::size_t count) {
    if (count == 0U) {
        return std::string{};
    }

    std::string joined = parts.front();
    for (std::size_t index = 1U; index < count; ++index) {
        joined += "/" + parts[index];
    }
    return joined;
}

std::vector<std::string> ZwaveController::splitTopic(const std::string& topic) {
    std::vector<std::string> parts{};
    std::size_t segmentStart = 0U;
    while (segmentStart <= topic.size()) {
        const std::size_t segmentEnd = topic.find('/', segmentStart);
        if (segmentEnd == std::string::npos) {
            parts.push_back(topic.substr(segmentStart));
            break;
        }

        parts.push_back(topic.substr(segmentStart, segmentEnd - segmentStart));
        segmentStart = segmentEnd + 1U;
    }
    return parts;
}

ZwaveNodeMap ZwaveController::buildNodeMap() const {
    ZwaveNodeMap result{};
    for (const auto& [nodeId, nodeState] : nodes_) {
        std::vector<ZwaveNodeObject> objects{};
        for (const auto& [classId, valueByIndex] : nodeState.classes) {
            for (const auto& [valueIndex, valueEvent] : valueByIndex) {
                (void)valueIndex;
                objects.push_back(ZwaveNodeObject{
                    .classId = classId,
                    .label = valueEvent.label.has_value() ? *valueEvent.label : std::string{},
                    .instance = valueEvent.instance,
                    .index = valueEvent.index,
                    .type = valueEvent.type
                });
            }
        }
        result.insert({nodeId, std::move(objects)});
    }
    return result;
}

ZwaveValueDescriptor ZwaveController::buildDescriptor(const ZwaveControllerValueEvent& event) {
    return ZwaveValueDescriptor{
        .nodeId = event.nodeId,
        .classId = event.classId,
        .instance = event.instance,
        .index = event.index,
        .label = event.label,
        .valueId = event.valueId};
}

std::string ZwaveController::notificationText(const ZwaveNotificationCode notification) {
    switch (notification) {
    case ZwaveNotificationCode::MessageComplete:
        return "message completed";
    case ZwaveNotificationCode::Timeout:
        return "timeout";
    case ZwaveNotificationCode::Nop:
        return "nop";
    case ZwaveNotificationCode::NodeAwake:
        return "node awake";
    case ZwaveNotificationCode::NodeSleep:
        return "node sleep";
    case ZwaveNotificationCode::NodeDead:
        return "node dead";
    case ZwaveNotificationCode::NodeAlive:
        return "node alive";
    default:
        return "unknown";
    }
}

void ZwaveController::publish(const std::string& topic, const Value& value, const std::string& reason) {
    publish(topic, value, reason, std::vector<ReasonEntry>{});
}

void ZwaveController::publish(
    const std::string& topic,
    const Value& value,
    const std::string& reason,
    const std::vector<ReasonEntry>& prependedReasons) {
    if (!publishCallback_) {
        return;
    }

    Message message{topic, value};
    message.addReason(reason);
    for (const auto& entry : prependedReasons | std::views::reverse) {
        message.addReason(entry.message, entry.timestamp);
    }
    publishCallback_(message);
}

void ZwaveController::publishValue(
    const std::uint16_t nodeId,
    const ZwaveControllerValueEvent& event,
    std::string reason) {
    try {
        std::string topic{};
        Value outputValue = event.value;
        std::vector<ReasonEntry> prependedReasons{};
        if (nodeId == kUsbControllerNodeId) {
            topic = usb_.topic;
        } else {
            const std::optional<ZwaveTopicMapping> mapping = devicesMapper_.valueToTopicAndType(buildDescriptor(event));
            if (!mapping.has_value() || mapping->topic.empty()) {
                throw std::runtime_error("missing topic mapping");
            }

            topic = mapping->topic;
            reason += ", Zwave value: " + valueToString(event.value);
            outputValue = applySwitchOutboundConversion(event.value, mapping->type);
            prependedReasons = takeMatchingPendingReasons(topic, event, outputValue).reasons;
        }

        publish(topic, outputValue, reason, prependedReasons);
    } catch (...) {
        publish("$MONITOR/zwave/" + std::to_string(nodeId), event.value, reason);
    }
}

bool ZwaveController::valuesEquivalent(const Value& leftValue, const Value& rightValue) {
    if (const auto* leftText = std::get_if<std::string>(&leftValue); leftText != nullptr) {
        const auto* rightText = std::get_if<std::string>(&rightValue);
        return rightText != nullptr && *leftText == *rightText;
    }

    const auto* leftNumber = std::get_if<double>(&leftValue);
    const auto* rightNumber = std::get_if<double>(&rightValue);
    if (leftNumber == nullptr || rightNumber == nullptr) {
        const std::optional<bool> leftSemanticBool = valueAsSemanticBool(leftValue);
        const std::optional<bool> rightSemanticBool = valueAsSemanticBool(rightValue);
        return leftSemanticBool.has_value() && rightSemanticBool.has_value()
            && *leftSemanticBool == *rightSemanticBool;
    }

    return std::fabs(*leftNumber - *rightNumber) < kIntegerTolerance;
}

Value ZwaveController::writeValueToExpectedValue(const ZwaveWriteRequest& writeRequest) {
    if (const auto* boolValue = std::get_if<bool>(&writeRequest.value); boolValue != nullptr) {
        return Value{*boolValue ? 1.0 : 0.0};
    }

    if (const auto* numericValue = std::get_if<double>(&writeRequest.value); numericValue != nullptr) {
        return Value{*numericValue};
    }

    return Value{std::get<std::string>(writeRequest.value)};
}

Value ZwaveController::toExpectedOutboundValue(const Value& value, const std::string& typeName) {
    return applySwitchOutboundConversion(value, typeName);
}

void ZwaveController::rememberPendingCommand(
    const std::string& replyTopic,
    const ZwaveWriteRequest& writeRequest,
    const std::vector<ReasonEntry>& reasons) {
    PendingCommand pendingCommand{
        .replyTopic = replyTopic,
        .target = writeRequest.target,
        .expectedValue = toExpectedOutboundValue(writeValueToExpectedValue(writeRequest), writeRequest.target.type),
        .reasons = reasons,
        .sentAt = std::chrono::steady_clock::now(),
        .lastPollAt = std::chrono::steady_clock::time_point{}}
    ;

    std::scoped_lock lock{pendingCommandsMutex_};
    bool replacedExistingEntry = false;
    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        const bool sameReplyTopic = iterator->replyTopic == pendingCommand.replyTopic;
        const bool sameTarget = iterator->target.nodeId == pendingCommand.target.nodeId
            && iterator->target.classId == pendingCommand.target.classId
            && iterator->target.instance == pendingCommand.target.instance
            && iterator->target.index == pendingCommand.target.index;
        const bool sameExpectedValue = valuesEquivalent(iterator->expectedValue, pendingCommand.expectedValue);
        if (sameReplyTopic && sameTarget && sameExpectedValue) {
            replacedExistingEntry = true;
            iterator = pendingCommands_.erase(iterator);
            continue;
        }
        ++iterator;
    }
    pendingCommands_.push_back(std::move(pendingCommand));

    std::ostringstream trace{};
    trace << "track command topic=" << replyTopic
          << " node=" << writeRequest.target.nodeId
          << " class=" << writeRequest.target.classId
          << " instance=" << static_cast<unsigned int>(writeRequest.target.instance)
          << " index=" << static_cast<unsigned int>(writeRequest.target.index)
          << " expected=" << valueToString(writeValueToExpectedValue(writeRequest))
          << " reasons=" << reasons.size()
          << " replaced=" << (replacedExistingEntry ? "1" : "0")
          << " pending_total=" << pendingCommands_.size();
    logPendingPollingTrace(trace.str());
}

ZwaveController::PendingCommandMatch ZwaveController::takeMatchingPendingReasons(
    const std::string& replyTopic,
    const ZwaveControllerValueEvent& event,
    const Value& outboundValue) {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto nowValue = std::chrono::steady_clock::now();

    std::ostringstream feedbackTrace{};
    feedbackTrace << "feedback topic=" << replyTopic
                  << " node=" << event.nodeId
                  << " class=" << event.classId
                  << " instance=" << static_cast<unsigned int>(event.instance)
                  << " index=" << static_cast<unsigned int>(event.index)
                  << " value=" << valueToString(outboundValue)
                  << " pending_total=" << pendingCommands_.size();
    logPendingPollingTrace(feedbackTrace.str());

    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
            std::ostringstream timeoutTrace{};
            timeoutTrace << "drop timed-out before match topic=" << iterator->replyTopic
                         << " node=" << iterator->target.nodeId
                         << " class=" << iterator->target.classId
                         << " instance=" << static_cast<unsigned int>(iterator->target.instance)
                         << " index=" << static_cast<unsigned int>(iterator->target.index);
            logPendingPollingTrace(timeoutTrace.str());
            iterator = pendingCommands_.erase(iterator);
            continue;
        }

        const bool sameReplyTopic = iterator->replyTopic == replyTopic;
        const bool sameTarget = iterator->target.nodeId == event.nodeId
            && iterator->target.classId == event.classId
            && iterator->target.instance == event.instance
            && iterator->target.index == event.index;
        const bool sameExpectedValue = valuesEquivalent(iterator->expectedValue, outboundValue);
        if (sameReplyTopic && sameTarget && sameExpectedValue) {
            std::vector<ReasonEntry> reasons = iterator->reasons;
            std::ostringstream matchTrace{};
            matchTrace << "match topic=" << replyTopic
                       << " node=" << event.nodeId
                       << " class=" << event.classId
                       << " instance=" << static_cast<unsigned int>(event.instance)
                       << " index=" << static_cast<unsigned int>(event.index)
                       << " prepended_reasons=" << reasons.size();
            logPendingPollingTrace(matchTrace.str());
            pendingCommands_.erase(iterator);
            return PendingCommandMatch{.matched = true, .reasons = std::move(reasons)};
        }

        std::ostringstream mismatchTrace{};
        mismatchTrace << "pending candidate mismatch"
                      << " candidate_topic=" << iterator->replyTopic
                      << " candidate_node=" << iterator->target.nodeId
                      << " candidate_class=" << iterator->target.classId
                      << " candidate_instance=" << static_cast<unsigned int>(iterator->target.instance)
                      << " candidate_index=" << static_cast<unsigned int>(iterator->target.index)
                      << " candidate_expected=" << valueToString(iterator->expectedValue)
                      << " same_topic=" << (sameReplyTopic ? "1" : "0")
                      << " same_target=" << (sameTarget ? "1" : "0")
                      << " same_value=" << (sameExpectedValue ? "1" : "0");
        logPendingPollingTrace(mismatchTrace.str());

        ++iterator;
    }

    logPendingPollingTrace("no pending match for feedback");

    return PendingCommandMatch{};
}

void ZwaveController::pollPendingCommands() {
    const auto nowValue = std::chrono::steady_clock::now();
    std::unordered_set<std::uint16_t> nodesToPoll{};

    {
        std::scoped_lock lock{pendingCommandsMutex_};
        auto iterator = pendingCommands_.begin();
        while (iterator != pendingCommands_.end()) {
            if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
                std::ostringstream timeoutTrace{};
                timeoutTrace << "drop timed-out pending topic=" << iterator->replyTopic
                             << " node=" << iterator->target.nodeId
                             << " class=" << iterator->target.classId
                             << " instance=" << static_cast<unsigned int>(iterator->target.instance)
                             << " index=" << static_cast<unsigned int>(iterator->target.index);
                logPendingPollingTrace(timeoutTrace.str());
                iterator = pendingCommands_.erase(iterator);
                continue;
            }

            if (nowValue - iterator->lastPollAt >= commandReactionPollInterval_) {
                iterator->lastPollAt = nowValue;
                nodesToPoll.insert(iterator->target.nodeId);
            }
            ++iterator;
        }

        if (!nodesToPoll.empty()) {
            std::ostringstream pollTrace{};
            pollTrace << "poll cycle pending_total=" << pendingCommands_.size() << " nodes=";
            bool first = true;
            for (const auto nodeId : nodesToPoll) {
                if (!first) {
                    pollTrace << ",";
                }
                first = false;
                pollTrace << nodeId;
            }
            logPendingPollingTrace(pollTrace.str());
        }
    }

    for (const auto nodeId : nodesToPoll) {
        driverPort_.requestNodeState(nodeId);
    }
}

void ZwaveController::runPendingCommandPollLoop() {
    while (!pendingCommandPollStop_.load()) {
        try {
            pollPendingCommands();
        } catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{kPendingCommandLoopSleepMs});
    }
}

void ZwaveController::storeNodeValue(const ZwaveControllerValueEvent& event) {
    auto nodeIterator = nodes_.find(event.nodeId);
    if (nodeIterator == nodes_.end()) {
        nodeIterator = nodes_.insert({event.nodeId, NodeRuntimeState{}}).first;
    }

    nodeIterator->second.classes[event.classId][event.index] = event;
}

} // namespace yaha
