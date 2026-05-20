#include "yaha/zwave_controller/zwave_controller.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <limits>
#include <mutex>
#include <ranges>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cctype>
#include <thread>
#include <unordered_set>
#include <utility>

namespace yaha {

namespace {

constexpr std::size_t kSetTopicMinimumParts = 2U;
constexpr std::uint16_t kUsbControllerNodeId = 1U;
constexpr double kIntegerTolerance = 1e-9;
constexpr std::uint32_t kPendingCommandLoopSleepMs = 20U;
constexpr unsigned char kJsonControlThreshold = 0x20U;
constexpr std::string_view kMonitorZwavePrefix = "$MONITOR/zwave";

const std::regex& iso8601TimestampRegex() {
    static const std::regex regex{
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+\-]\d{2}:\d{2})$)",
        std::regex::ECMAScript};
    return regex;
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

[[nodiscard]] bool isSpecCompliantReasonTimestamp(const std::string& timestamp) {
    if (timestamp.empty()) {
        return false;
    }
    return std::regex_match(timestamp, iso8601TimestampRegex());
}

[[nodiscard]] std::string buildZwaveNetworkReason(const std::optional<std::uint64_t>& valueId) {
    if (!valueId.has_value()) {
        return "received from zwave network";
    }
    return "received from zwave network id: " + std::to_string(*valueId);
}

[[nodiscard]] std::string sanitizeReasonMessageForJson(std::string text) {
    for (char& character : text) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        if (unsignedCharacter < kJsonControlThreshold && character != '\n' && character != '\r' && character != '\t') {
            character = ' ';
        }
    }
    return text;
}

void addSpecCompliantReason(Message& message, const ReasonEntry& reasonEntry) {
    const std::string sanitizedMessage = sanitizeReasonMessageForJson(reasonEntry.message);
    if (sanitizedMessage.empty()) {
        return;
    }

    if (isSpecCompliantReasonTimestamp(reasonEntry.timestamp)) {
        message.addReason(sanitizedMessage, reasonEntry.timestamp);
        return;
    }

    message.addReason(sanitizedMessage);
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
        replyTopic = deviceTopic;
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
    publish(std::string{kMonitorZwavePrefix} + "/scan/state", std::string{"scanning"}, reason.str());
}

void ZwaveController::onDriverFailed() {
    publish(std::string{kMonitorZwavePrefix} + "/driver/error/state",
            std::string{"driver_failed"},
            "failed to start driver. Stopping module");
    publish(std::string{kMonitorZwavePrefix} + "/scan/result", std::string{"scanning_failed"}, "driver failed");

    if (driverFailedCallback_) {
        driverFailedCallback_();
    }
}

void ZwaveController::setDriverFailedCallback(std::function<void()> callback) {
    driverFailedCallback_ = std::move(callback);
}

void ZwaveController::onScanComplete() {
    publish(std::string{kMonitorZwavePrefix} + "/scan/state", std::string{"idle"}, "scan completed");
    publish(std::string{kMonitorZwavePrefix} + "/scan/result", std::string{"scanning_completed"}, "zwave info");
}

void ZwaveController::onNotification(const std::uint16_t nodeId, const ZwaveNotificationCode notification) {
    auto nodeIterator = nodes_.find(nodeId);
    if (nodeIterator == nodes_.end()) {
        nodeIterator = nodes_.insert({nodeId, NodeRuntimeState{}}).first;
    }

    if (notification == ZwaveNotificationCode::NodeDead) {
        nodeIterator->second.dead = true;
    } else if (notification == ZwaveNotificationCode::NodeAlive) {
        nodeIterator->second.dead = false;
    }

    try {
        switch (notification) {
        case ZwaveNotificationCode::NodeDead:
            publishNodeState(nodeId, "health", "dead", "zwave notification node=" + std::to_string(nodeId));
            updateNodeCommState(nodeId, NodeCommState::Timeout, "node reported dead");
            return;
        case ZwaveNotificationCode::NodeAlive:
            publishNodeState(nodeId, "health", "alive", "zwave notification node=" + std::to_string(nodeId));
            updateNodeCommState(nodeId, NodeCommState::Ok, "node communication succeeded");
            clearNodeErrorState(nodeId, "node communication recovered");
            return;
        case ZwaveNotificationCode::NodeAwake:
            publishNodeState(nodeId, "power_state", "awake", "zwave notification node=" + std::to_string(nodeId));
            updateNodeCommState(nodeId, NodeCommState::Ok, "node communication succeeded");
            clearNodeErrorState(nodeId, "node communication recovered");
            return;
        case ZwaveNotificationCode::NodeSleep:
            publishNodeState(nodeId, "power_state", "sleep", "zwave notification node=" + std::to_string(nodeId));
            updateNodeCommState(nodeId, NodeCommState::Ok, "node communication succeeded");
            clearNodeErrorState(nodeId, "node communication recovered");
            return;
        case ZwaveNotificationCode::Timeout:
            updateNodeCommState(nodeId, NodeCommState::Timeout, "zwave notification node=" + std::to_string(nodeId));
            return;
        case ZwaveNotificationCode::MessageComplete:
        case ZwaveNotificationCode::Nop:
            return;
        }
    } catch (...) {
        publishNodeErrorState(nodeId,
                              "publish_failed",
                              ErrorStateSeverity::PublishFailed,
                              "failed to publish notification state");
    }
}

void ZwaveController::onControllerCommand(const std::int32_t resultCode, const std::string& statusText) {
    publish(
        std::string{kMonitorZwavePrefix} + "/controller/command/last_status",
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
    nodeIterator->second.dead = false;
}

void ZwaveController::onValueAdded(const ZwaveControllerValueEvent& event) {
    storeNodeValue(event);
    cacheLastKnownTopicState(event);
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
    cacheLastKnownTopicState(event);

    publishValue(event.nodeId, event, buildZwaveNetworkReason(event.valueId));
    updateNodeCommState(event.nodeId, NodeCommState::Ok, "node communication succeeded");
    clearNodeErrorState(event.nodeId, "node communication recovered");
}

void ZwaveController::onValueRefreshed(
    const std::uint16_t nodeId,
    const std::uint16_t classId,
    const ZwaveControllerValueEvent& event) {
    (void)nodeId;
    (void)classId;
    storeNodeValue(event);
    cacheLastKnownTopicState(event);
    updateNodeCommState(event.nodeId, NodeCommState::Ok, "node communication succeeded");
    clearNodeErrorState(event.nodeId, "node communication recovered");

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

        publish(mapping->topic, outboundValue, buildZwaveNetworkReason(event.valueId), pendingMatch.reasons);
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
        return "unknown_notification";
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
        addSpecCompliantReason(message, entry);
    }
    publishCallback_(message);
}

void ZwaveController::publishValue(
    const std::uint16_t nodeId,
    const ZwaveControllerValueEvent& event,
    const std::string& reason) {
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
            outputValue = applySwitchOutboundConversion(event.value, mapping->type);
            prependedReasons = takeMatchingPendingReasons(topic, event, outputValue).reasons;
        }

        publish(topic, outputValue, reason, prependedReasons);
    } catch (...) {
        const std::string topic = buildNodeBaseTopic(nodeId)
            + "/class/" + std::to_string(event.classId)
            + "/instance/" + std::to_string(event.instance)
            + "/index/" + std::to_string(event.index)
            + "/value/unmapped";
        publish(topic, event.value, reason);
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

void ZwaveController::cacheLastKnownTopicState(const ZwaveControllerValueEvent& event) {
    std::string topic{};
    Value cachedValue = event.value;

    if (event.nodeId == kUsbControllerNodeId) {
        topic = usb_.topic;
    } else {
        const std::optional<ZwaveTopicMapping> mapping = devicesMapper_.valueToTopicAndType(buildDescriptor(event));
        if (!mapping.has_value() || mapping->topic.empty()) {
            return;
        }

        topic = mapping->topic;
        cachedValue = applySwitchOutboundConversion(event.value, mapping->type);
    }

    std::scoped_lock lock{cachedTopicStatesMutex_};
    cachedTopicStates_[topic] = CachedTopicState{.value = cachedValue, .valueId = event.valueId};
}

std::optional<ZwaveController::CachedTopicState> ZwaveController::findCachedTopicState(const std::string& topic) const {
    std::scoped_lock lock{cachedTopicStatesMutex_};
    const auto iterator = cachedTopicStates_.find(topic);
    if (iterator == cachedTopicStates_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

void ZwaveController::publishTimeoutForPendingCommand(const PendingCommand& pendingCommand) {
    const std::optional<CachedTopicState> cachedState = findCachedTopicState(pendingCommand.replyTopic);
    if (!cachedState.has_value()) {
        return;
    }

    const std::string timeoutReason = cachedState->valueId.has_value()
        ? "timeout waiting for zwave network id: " + std::to_string(*cachedState->valueId)
        : "timeout waiting for zwave network id: unknown";

    publish(pendingCommand.replyTopic, cachedState->value, timeoutReason, pendingCommand.reasons);
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
    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        const bool sameReplyTopic = iterator->replyTopic == pendingCommand.replyTopic;
        const bool sameTarget = iterator->target.nodeId == pendingCommand.target.nodeId
            && iterator->target.classId == pendingCommand.target.classId
            && iterator->target.instance == pendingCommand.target.instance
            && iterator->target.index == pendingCommand.target.index;
        const bool sameExpectedValue = valuesEquivalent(iterator->expectedValue, pendingCommand.expectedValue);
        if (sameReplyTopic && sameTarget && sameExpectedValue) {
            iterator = pendingCommands_.erase(iterator);
            continue;
        }
        ++iterator;
    }
    pendingCommands_.push_back(std::move(pendingCommand));
}

ZwaveController::PendingCommandMatch ZwaveController::takeMatchingPendingReasons(
    const std::string& replyTopic,
    const ZwaveControllerValueEvent& event,
    const Value& outboundValue) {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto nowValue = std::chrono::steady_clock::now();

    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
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
            pendingCommands_.erase(iterator);
            return PendingCommandMatch{.matched = true, .reasons = std::move(reasons)};
        }

        ++iterator;
    }

    return PendingCommandMatch{};
}

void ZwaveController::pollPendingCommands() {
    const auto nowValue = std::chrono::steady_clock::now();
    std::unordered_set<std::uint16_t> nodesToPoll{};
    std::vector<PendingCommand> timedOutCommands{};

    {
        std::scoped_lock lock{pendingCommandsMutex_};
        auto iterator = pendingCommands_.begin();
        while (iterator != pendingCommands_.end()) {
            if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
                timedOutCommands.push_back(*iterator);
                iterator = pendingCommands_.erase(iterator);
                continue;
            }

            if (nowValue - iterator->lastPollAt >= commandReactionPollInterval_) {
                iterator->lastPollAt = nowValue;
                nodesToPoll.insert(iterator->target.nodeId);
            }
            ++iterator;
        }
    }

    for (const auto& timedOutCommand : timedOutCommands) {
        publishTimeoutForPendingCommand(timedOutCommand);
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

void ZwaveController::publishNodeState(
    const std::uint16_t nodeId,
    const std::string& stateName,
    const std::string& value,
    const std::string& reason) {
    publish(buildNodeBaseTopic(nodeId) + "/" + stateName, Value{value}, reason);
}

void ZwaveController::publishNodeErrorState(
    const std::uint16_t nodeId,
    const std::string& value,
    const ErrorStateSeverity severity,
    const std::string& reason) {
    std::scoped_lock lock{nodeErrorStatesMutex_};
    const ErrorStateSeverity currentSeverity = [&] {
        const auto iterator = nodeErrorStates_.find(nodeId);
        if (iterator == nodeErrorStates_.end()) {
            return ErrorStateSeverity::NoError;
        }
        return iterator->second;
    }();

    if (static_cast<std::uint8_t>(severity) < static_cast<std::uint8_t>(currentSeverity)) {
        return;
    }

    nodeErrorStates_[nodeId] = severity;
    publish(buildNodeBaseTopic(nodeId) + "/error/state", Value{value}, reason);
}

void ZwaveController::updateNodeCommState(
    const std::uint16_t nodeId,
    const NodeCommState targetState,
    const std::string& reason) {
    std::scoped_lock lock{nodeCommStatesMutex_};
    const NodeCommState currentState = [&] {
        const auto iterator = nodeCommStates_.find(nodeId);
        if (iterator == nodeCommStates_.end()) {
            return NodeCommState::Ok;
        }
        return iterator->second;
    }();

    if (currentState == targetState) {
        return;
    }

    nodeCommStates_[nodeId] = targetState;
    const std::string value = targetState == NodeCommState::Ok ? "ok" : "timeout";
    publishNodeState(nodeId, "comm/state", value, reason);
}

void ZwaveController::clearNodeErrorState(const std::uint16_t nodeId, const std::string& reason) {
    std::scoped_lock lock{nodeErrorStatesMutex_};
    const auto iterator = nodeErrorStates_.find(nodeId);
    if (iterator == nodeErrorStates_.end() || iterator->second == ErrorStateSeverity::NoError) {
        return;
    }

    iterator->second = ErrorStateSeverity::NoError;
    publish(buildNodeBaseTopic(nodeId) + "/error/state", Value{std::string{"no_error"}}, reason);
}

std::string ZwaveController::buildNodeBaseTopic(const std::uint16_t nodeId) {
    return std::string{kMonitorZwavePrefix} + "/node/" + std::to_string(nodeId);
}

} // namespace yaha
