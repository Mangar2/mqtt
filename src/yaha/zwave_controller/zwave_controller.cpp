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
constexpr std::uint16_t kConfigCommandClassId = 0x70U;
constexpr double kIntegerTolerance = 1e-9;
constexpr std::uint32_t kPendingCommandLoopSleepMs = 20U;
constexpr unsigned char kJsonControlThreshold = 0x20U;
constexpr std::string_view kMonitorZwavePrefix = "$MONITOR/zwave";
constexpr std::string_view kSystemZwavePrefix = "system/zwave";

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
    return std::fabs(std::get<double>(value)) >= kIntegerTolerance;
}

[[nodiscard]] Value applySwitchOutboundConversion(const Value& value, const std::string& typeName) {
    if (typeName != "switch") {
        return value;
    }

    return valueAsBool(value) ? Value{std::string{"on"}} : Value{std::string{"off"}};
}

[[nodiscard]] std::optional<bool> valueAsSemanticBool(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        if (std::fabs(*numericValue) < kIntegerTolerance) {
            return false;
        }
        if (std::isfinite(*numericValue)) {
            return true;
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

[[nodiscard]] std::string buildZwaveNetworkReason(const std::uint16_t nodeId,
                                                   const std::optional<std::uint64_t>& valueId) {
    std::string reason = "received from zwave network node: " + std::to_string(nodeId);
    if (!valueId.has_value()) {
        return reason;
    }
    return reason + " id: " + std::to_string(*valueId);
}

[[nodiscard]] std::string buildValueEventCommunicationReason(
    const ZwaveControllerValueEvent& event,
    const std::string_view sourceName) {
    std::string reason = "node " + std::to_string(event.nodeId)
        + " communication succeeded; source=" + std::string{sourceName}
        + " target=node/" + std::to_string(event.nodeId)
        + "/class/" + std::to_string(event.classId)
        + "/instance/" + std::to_string(event.instance)
        + "/index/" + std::to_string(event.index);

    if (event.valueId.has_value()) {
        reason += " valueId=" + std::to_string(*event.valueId);
    } else {
        reason += " valueId=unknown";
    }

    return reason;
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

[[nodiscard]] std::string valueToDebugText(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        std::ostringstream stream{};
        stream << *numericValue;
        return stream.str();
    }
    return std::get<std::string>(value);
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
    const std::int64_t fullDevicePollIntervalMs,
    const std::int64_t commandReactionPollIntervalMs,
    const std::int64_t commandReactionTimeoutMs,
    const std::uint32_t unresponsiveInputTimeoutMs,
    const std::size_t unresponsiveTimeoutErrorThreshold)
    : usb_(std::move(usbConfig))
    , driverPort_(driverPort)
    , devicesMapper_(std::vector<ZwaveDeviceConfig>{})
    , fullDevicePollInterval_(fullDevicePollIntervalMs)
    , commandReactionPollInterval_(commandReactionPollIntervalMs)
    , commandReactionTimeout_(commandReactionTimeoutMs)
    , unresponsiveInputTimeout_(unresponsiveInputTimeoutMs)
    , unresponsiveTimeoutErrorThreshold_(std::max<std::size_t>(1U, unresponsiveTimeoutErrorThreshold)) {
    lastFullDevicePollAt_ = std::chrono::steady_clock::now();
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
    std::scoped_lock lock{devicesMutex_};
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
    std::vector<ZwaveDeviceConfig> deviceSnapshot{};
    {
        std::scoped_lock lock{devicesMutex_};
        deviceSnapshot = devices_;
    }

    for (const auto& device : deviceSnapshot) {
        driverPort_.requestAllConfigParams(device.nodeId);
    }
}

std::vector<std::uint16_t> ZwaveController::knownNodeIds() const {
    std::vector<ZwaveDeviceConfig> deviceSnapshot{};
    {
        std::scoped_lock lock{devicesMutex_};
        deviceSnapshot = devices_;
    }

    std::vector<std::uint16_t> nodeIds{};
    nodeIds.reserve(nodes_.size() + deviceSnapshot.size());

    for (const auto nodeId : nodes_ | std::views::keys) {
        nodeIds.push_back(nodeId);
    }
    for (const auto& device : deviceSnapshot) {
        nodeIds.push_back(device.nodeId);
    }
    std::ranges::sort(nodeIds);
    nodeIds.erase(std::ranges::unique(nodeIds).begin(), nodeIds.end());
    return nodeIds;
}

void ZwaveController::close() {
    driverPort_.disconnect(usb_.device);
}

void ZwaveController::onDriverReady(const std::uint32_t homeId) {
    std::ostringstream reason{};
    reason << "scanning homeid=0x" << std::hex << homeId;
    publish(std::string{kSystemZwavePrefix} + "/scan", std::string{"scanning"}, reason.str());
}

void ZwaveController::onDriverFailed() {
    publish(std::string{kMonitorZwavePrefix} + "/driver/error/state",
            std::string{"driver_failed"},
            "failed to start driver. Stopping module");
    publish(std::string{kSystemZwavePrefix} + "/scan", std::string{"failed"}, "driver failed");

    if (driverFailedCallback_) {
        driverFailedCallback_();
    }
}

void ZwaveController::setDriverFailedCallback(std::function<void()> callback) {
    driverFailedCallback_ = std::move(callback);
}

void ZwaveController::setUnresponsiveNetworkCallback(std::function<void()> callback) {
    std::scoped_lock lock{unresponsiveNetworkMutex_};
    unresponsiveNetworkCallback_ = std::move(callback);
}

void ZwaveController::onScanComplete() {
    publish(std::string{kSystemZwavePrefix} + "/scan", std::string{"off"}, "scan completed");
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
            updateNodeHealthState(
                nodeId,
                NodeHealthState::Dead,
                "node " + std::to_string(nodeId) + " sent \"death\" information");
            updateNodeCommState(nodeId, NodeCommState::Timeout, "node " + std::to_string(nodeId) + " reported dead");
            return;
        case ZwaveNotificationCode::NodeAlive:
            markSuccessfulZwaveInput();
            updateNodeHealthState(
                nodeId,
                NodeHealthState::Alive,
                "node " + std::to_string(nodeId) + " sent \"alive\" information");
            return;
        case ZwaveNotificationCode::NodeAwake:
            markSuccessfulZwaveInput();
            publishNodeState(
                nodeId,
                "power_state",
                "awake",
                "node " + std::to_string(nodeId) + " sent \"awake\" information");
            updateNodeHealthState(
                nodeId,
                NodeHealthState::Alive,
                "node " + std::to_string(nodeId) + " sent first value information");
            return;
        case ZwaveNotificationCode::NodeSleep:
            publishNodeState(
                nodeId,
                "power_state",
                "sleep",
                "node " + std::to_string(nodeId) + " sent \"sleep\" information");
            updateNodeHealthState(
                nodeId,
                NodeHealthState::Alive,
                "node " + std::to_string(nodeId) + " sent first value information");
            return;
        case ZwaveNotificationCode::Timeout:
            {
                trackTimeoutDropAndTriggerIfNeeded();
                const std::string reason =
                    "node " + std::to_string(nodeId) + " sent \"timeout\" information; " + describeTimeoutSource(nodeId);
            updateNodeCommState(
                nodeId,
                NodeCommState::Timeout,
                    reason);
            }
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

void ZwaveController::onControllerCommand(
    const std::uint16_t nodeId,
    const std::int32_t resultCode,
    const std::string& statusText) {
    publish(
        std::string{kMonitorZwavePrefix} + "/controller/command/last_status",
        statusText,
        "controller commmand feedback: r=" + std::to_string(resultCode) + " s=" + statusText);

    if (nodeId == 0U) {
        return;
    }
}

void ZwaveController::onNodeAdded(const std::uint16_t nodeId) {
    nodes_[nodeId] = NodeRuntimeState{};
    {
        std::scoped_lock lock{includeFlowCandidateNodeIdsMutex_};
        includeFlowCandidateNodeIds_.insert(nodeId);
    }
}

void ZwaveController::onNodeRemoved(const std::uint16_t nodeId) {
    nodes_.erase(nodeId);

    {
        std::scoped_lock lock{nodeErrorStatesMutex_};
        nodeErrorStates_.erase(nodeId);
    }
    {
        std::scoped_lock lock{nodeCommStatesMutex_};
        nodeCommStates_.erase(nodeId);
    }
    {
        std::scoped_lock lock{nodeHealthStatesMutex_};
        nodeHealthStates_.erase(nodeId);
    }
    {
        std::scoped_lock lock{nodeIncludeStatesMutex_};
        nodeIncludeStates_.erase(nodeId);
    }
    {
        std::scoped_lock lock{includeFlowCandidateNodeIdsMutex_};
        includeFlowCandidateNodeIds_.erase(nodeId);
    }

    {
        std::scoped_lock lock{pendingCommandsMutex_};
        const auto remainingRange = std::ranges::remove_if(
            pendingCommands_,
            [nodeId](const PendingCommand& pendingCommand) {
                return pendingCommand.target.nodeId == nodeId;
            });
        pendingCommands_.erase(remainingRange.begin(), remainingRange.end());
    }

    {
        const std::string keyPrefix = std::to_string(nodeId) + ":";
        std::scoped_lock lock{publishedConfigCapabilityKeysMutex_};
        auto iterator = publishedConfigCapabilityKeys_.begin();
        while (iterator != publishedConfigCapabilityKeys_.end()) {
            if (iterator->starts_with(keyPrefix)) {
                iterator = publishedConfigCapabilityKeys_.erase(iterator);
                continue;
            }
            ++iterator;
        }
    }

    std::vector<ZwaveDeviceConfig> deviceSnapshot{};
    {
        std::scoped_lock lock{devicesMutex_};
        deviceSnapshot = devices_;
    }

    std::vector<std::string> topicsToErase{};
    topicsToErase.reserve(deviceSnapshot.size());
    for (const auto& device : deviceSnapshot) {
        if (device.nodeId == nodeId && !device.topic.empty()) {
            topicsToErase.push_back(device.topic);
        }
    }

    if (!topicsToErase.empty()) {
        std::scoped_lock lock{cachedTopicStatesMutex_};
        for (const auto& topic : topicsToErase) {
            cachedTopicStates_.erase(topic);
        }
    }
}

void ZwaveController::onNodeReady(
    const std::uint16_t nodeId,
    const ZwaveNodeInfo& nodeInfo,
    const std::string& queryStage) {
    auto nodeIterator = nodes_.find(nodeId);
    if (nodeIterator == nodes_.end()) {
        nodeIterator = nodes_.insert({nodeId, NodeRuntimeState{}}).first;
    }

    nodeIterator->second.info = nodeInfo;
    nodeIterator->second.ready = true;
    nodeIterator->second.dead = false;

    if (queryStage == "queries_complete") {
        driverPort_.enablePoll(nodeId, kZwaveSwitchBinaryClass);
        driverPort_.enablePoll(nodeId, kZwaveSwitchMultilevelClass);

        bool includeFlowCandidateReachedCompletion = false;
        {
            std::scoped_lock lock{includeFlowCandidateNodeIdsMutex_};
            const auto candidateIterator = includeFlowCandidateNodeIds_.find(nodeId);
            if (candidateIterator != includeFlowCandidateNodeIds_.end()) {
                includeFlowCandidateReachedCompletion = true;
                includeFlowCandidateNodeIds_.erase(candidateIterator);
            }
        }

        if (includeFlowCandidateReachedCompletion) {
            publishNodeIncludeState(nodeId, "included", "include flow completed", true);
        }
    }
}

void ZwaveController::onValueAdded(const ZwaveControllerValueEvent& event) {
    markSuccessfulZwaveInput();
    storeNodeValue(event);
    cacheLastKnownTopicState(event);
    publishConfigParameterCapabilities(event);

    const auto nodeIterator = nodes_.find(event.nodeId);
    if (nodeIterator != nodes_.end() && nodeIterator->second.ready) {
        driverPort_.enablePoll(event.nodeId, event.classId);
    }

    updateNodeHealthState(
        event.nodeId,
        NodeHealthState::Alive,
        "node " + std::to_string(event.nodeId) + " sent first value information");
}

void ZwaveController::onValueRemoved(const std::uint16_t nodeId, const std::uint16_t classId, const std::uint8_t index) {
    auto nodeIterator = nodes_.find(nodeId);
    if (nodeIterator == nodes_.end()) {
        return;
    }

    auto classIterator = nodeIterator->second.classes.find(classId);
    if (classIterator == nodeIterator->second.classes.end()) {
        return;
    }

    classIterator->second.erase(index);
    if (classIterator->second.empty()) {
        nodeIterator->second.classes.erase(classIterator);
    }
}

void ZwaveController::onValueChanged(const ZwaveControllerValueEvent& event) {
    markSuccessfulZwaveInput();
    storeNodeValue(event);
    cacheLastKnownTopicState(event);
    publishConfigParameterCapabilities(event);

    auto nodeIterator = nodes_.find(event.nodeId);
    if (nodeIterator != nodes_.end()) {
        nodeIterator->second.dead = false;
        if (nodeIterator->second.ready) {
            driverPort_.enablePoll(event.nodeId, event.classId);
        }
    }
    updateNodeHealthState(
        event.nodeId,
        NodeHealthState::Alive,
        "node " + std::to_string(event.nodeId) + " sent value information");

    publishValue(event.nodeId, event, buildZwaveNetworkReason(event.nodeId, event.valueId));
    updateNodeCommState(
        event.nodeId,
        NodeCommState::Ok,
        buildValueEventCommunicationReason(event, "openzwave_value_changed"));
    clearNodeErrorState(event.nodeId, "node " + std::to_string(event.nodeId) + " communication recovered");
}

void ZwaveController::onValueRefreshed(
    const std::uint16_t nodeId,
    const std::uint16_t classId,
    const ZwaveControllerValueEvent& event) {
    (void)nodeId;
    (void)classId;
    markSuccessfulZwaveInput();
    storeNodeValue(event);
    cacheLastKnownTopicState(event);

    auto nodeIterator = nodes_.find(event.nodeId);
    if (nodeIterator != nodes_.end()) {
        nodeIterator->second.dead = false;
    }

    updateNodeHealthState(
        event.nodeId,
        NodeHealthState::Alive,
        "node " + std::to_string(event.nodeId) + " sent value information");

    updateNodeCommState(
        event.nodeId,
        NodeCommState::Ok,
        buildValueEventCommunicationReason(event, "openzwave_value_refreshed"));
    clearNodeErrorState(event.nodeId, "node " + std::to_string(event.nodeId) + " communication recovered");

    publishValue(event.nodeId, event, buildZwaveNetworkReason(event.nodeId, event.valueId));
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
        const std::optional<std::string> baseTopic = resolveNodeMonitorBaseTopic(nodeId);
        const std::string topic = (baseTopic.has_value() ? *baseTopic : buildNodeBaseTopic(nodeId))
            + "/class/" + std::to_string(event.classId)
            + "/instance/" + std::to_string(event.instance)
            + "/index/" + std::to_string(event.index)
            + "/value/unmapped";
        publish(topic, event.value, reason);
    }
}

void ZwaveController::publishConfigParameterCapabilities(const ZwaveControllerValueEvent& event) {
    if (event.classId != kConfigCommandClassId) {
        return;
    }

    const std::optional<std::string> baseTopic = resolveNodeMonitorBaseTopic(event.nodeId);
    if (!baseTopic.has_value()) {
        return;
    }

    const std::string dedupeKey = std::to_string(event.nodeId) + ":" + std::to_string(event.instance)
        + ":" + std::to_string(event.index);
    {
        std::scoped_lock lock{publishedConfigCapabilityKeysMutex_};
        const auto [_, inserted] = publishedConfigCapabilityKeys_.insert(dedupeKey);
        if (!inserted) {
            return;
        }
    }

    const std::string parameterTopic = *baseTopic + "/config/param/" + std::to_string(event.index);
    const std::string reason = "discovered configuration parameter capability from "
        + buildZwaveNetworkReason(event.nodeId, event.valueId);

    publish(parameterTopic + "/supported", Value{std::string{"on"}}, reason);
    publish(parameterTopic + "/type", Value{event.type.empty() ? std::string{"unknown"} : event.type}, reason);
    publish(parameterTopic + "/read_only", Value{event.readOnly ? std::string{"on"} : std::string{"off"}}, reason);

    if (event.label.has_value() && !event.label->empty()) {
        publish(parameterTopic + "/label", Value{*event.label}, reason);
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

std::string ZwaveController::describeTimeoutSource(const std::uint16_t nodeId) {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto match = std::ranges::find_if(pendingCommands_, [nodeId](const PendingCommand& pendingCommand) {
        return pendingCommand.target.nodeId == nodeId;
    });

    if (match == pendingCommands_.end()) {
        return "source=openzwave_notification_timeout context=no_pending_command";
    }

    return "source=openzwave_notification_timeout context=pending_command topic=" + match->replyTopic
        + " target=node/" + std::to_string(match->target.nodeId)
        + "/class/" + std::to_string(match->target.classId)
        + "/instance/" + std::to_string(match->target.instance)
        + "/index/" + std::to_string(match->target.index)
        + " expected=" + valueToDebugText(match->expectedValue);
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
        const auto expectedSemanticBool = valueAsSemanticBool(iterator->expectedValue);
        const auto outboundSemanticBool = valueAsSemanticBool(outboundValue);
        const bool sameExpectedValue = valuesEquivalent(iterator->expectedValue, outboundValue)
            || (event.classId == kZwaveSwitchMultilevelClass
                && expectedSemanticBool.has_value()
                && outboundSemanticBool.has_value()
                && *expectedSemanticBool == *outboundSemanticBool);
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

void ZwaveController::pollConfiguredNodes() {
    const auto nowValue = std::chrono::steady_clock::now();
    if (nowValue - lastFullDevicePollAt_ < fullDevicePollInterval_) {
        return;
    }
    lastFullDevicePollAt_ = nowValue;

    std::vector<ZwaveDeviceConfig> deviceSnapshot{};
    {
        std::scoped_lock lock{devicesMutex_};
        deviceSnapshot = devices_;
    }

    std::unordered_set<std::uint16_t> nodeIds{};
    for (const auto& device : deviceSnapshot) {
        nodeIds.insert(device.nodeId);
    }

    for (const auto nodeId : nodeIds) {
        driverPort_.requestNodeState(nodeId);
    }
}

void ZwaveController::runPendingCommandPollLoop() {
    while (!pendingCommandPollStop_.load()) {
        try {
            pollPendingCommands();
            pollConfiguredNodes();
        } catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{kPendingCommandLoopSleepMs});
    }
}

void ZwaveController::markSuccessfulZwaveInput() {
    std::scoped_lock lock{unresponsiveNetworkMutex_};
    lastSuccessfulZwaveInputAt_ = std::chrono::steady_clock::now();
    timeoutErrorsSinceLastSuccess_ = 0U;
    unresponsiveNetworkCallbackTriggered_ = false;
}

void ZwaveController::trackTimeoutDropAndTriggerIfNeeded() {
    std::function<void()> callback{};

    {
        std::scoped_lock lock{unresponsiveNetworkMutex_};
        timeoutErrorsSinceLastSuccess_ += 1U;
        const auto nowValue = std::chrono::steady_clock::now();
        const bool noSuccessfulInputForTooLong = nowValue - lastSuccessfulZwaveInputAt_ >= unresponsiveInputTimeout_;
        const bool timeoutThresholdReached = timeoutErrorsSinceLastSuccess_ >= unresponsiveTimeoutErrorThreshold_;

        if (unresponsiveNetworkCallbackTriggered_ || !noSuccessfulInputForTooLong || !timeoutThresholdReached) {
            return;
        }

        unresponsiveNetworkCallbackTriggered_ = true;
        callback = unresponsiveNetworkCallback_;
    }

    if (callback) {
        callback();
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
    const std::optional<std::string> baseTopic = resolveNodeMonitorBaseTopic(nodeId);
    if (!baseTopic.has_value()) {
        return;
    }
    publish(*baseTopic + "/" + stateName, Value{value}, reason);
}

void ZwaveController::publishNodeIncludeState(
    const std::uint16_t nodeId,
    const std::string& value,
    const std::string& reason,
    const bool forcePublish) {
    {
        std::scoped_lock lock{nodeIncludeStatesMutex_};
        const auto stateIterator = nodeIncludeStates_.find(nodeId);
        if (!forcePublish && stateIterator != nodeIncludeStates_.end() && stateIterator->second == value) {
            return;
        }
        nodeIncludeStates_[nodeId] = value;
    }

    publish(buildNodeBaseTopic(nodeId) + "/include", Value{value}, reason);
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
    const std::optional<std::string> baseTopic = resolveNodeMonitorBaseTopic(nodeId);
    if (!baseTopic.has_value()) {
        return;
    }
    publish(*baseTopic + "/error/state", Value{value}, reason);
}

void ZwaveController::updateNodeHealthState(
    const std::uint16_t nodeId,
    const NodeHealthState targetState,
    const std::string& reason) {
    std::scoped_lock lock{nodeHealthStatesMutex_};
    const NodeHealthState currentState = [&] {
        const auto iterator = nodeHealthStates_.find(nodeId);
        if (iterator == nodeHealthStates_.end()) {
            return NodeHealthState::Unknown;
        }
        return iterator->second;
    }();

    if (currentState == targetState) {
        return;
    }

    nodeHealthStates_[nodeId] = targetState;
    const std::string value = targetState == NodeHealthState::Dead ? "dead" : "alive";
    publishNodeState(nodeId, "health", value, reason);
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
    const std::optional<std::string> baseTopic = resolveNodeMonitorBaseTopic(nodeId);
    if (!baseTopic.has_value()) {
        return;
    }
    publish(*baseTopic + "/error/state", Value{std::string{"no_error"}}, reason);
}

std::optional<std::string> ZwaveController::resolveNodeMonitorBaseTopic(const std::uint16_t nodeId) const {
    std::optional<std::string> preferredTopic{};
    std::optional<std::string> fallbackTopic{};

    for (const auto& device : devices_) {
        if (device.nodeId != nodeId || device.topic.empty()) {
            continue;
        }

        if (!device.classId.has_value()) {
            preferredTopic = device.topic;
            break;
        }

        if (!fallbackTopic.has_value() || device.topic.size() < fallbackTopic->size()) {
            fallbackTopic = device.topic;
        }
    }

    const std::optional<std::string> chosenTopic = preferredTopic.has_value() ? preferredTopic : fallbackTopic;
    if (!chosenTopic.has_value()) {
        return std::nullopt;
    }

    return std::string{"$MONITOR/"} + *chosenTopic;
}

std::string ZwaveController::buildNodeBaseTopic(const std::uint16_t nodeId) {
    return std::string{kMonitorZwavePrefix} + "/node/" + std::to_string(nodeId);
}

} // namespace yaha
