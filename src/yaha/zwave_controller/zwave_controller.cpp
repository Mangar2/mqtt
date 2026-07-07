#include "yaha/zwave_controller/zwave_controller.h"
#include "yaha/zwave_controller/zwave_controller_reason_utils.h"
#include "yaha/zwave_controller/zwave_controller_topic_utils.h"
#include "yaha/zwave_controller/zwave_controller_value_utils.h"
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>
namespace yaha {
namespace {

constexpr std::uint16_t kUsbControllerNodeId = 1U;
constexpr std::uint16_t kConfigCommandClassId = 0x70U;
constexpr std::string_view kMonitorZwavePrefix = "$MONITOR/zwave";
constexpr std::string_view kSystemZwavePrefix = "system/zwave";

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
    , polling_(
        std::chrono::milliseconds{fullDevicePollIntervalMs},
        std::chrono::milliseconds{commandReactionPollIntervalMs},
        std::chrono::milliseconds{commandReactionTimeoutMs},
        [this](const std::uint16_t nodeId) {
            driverPort_.requestNodeState(nodeId);
        },
        [this](
            const std::string& replyTopic,
            const Value& cachedValue,
            const std::optional<std::uint64_t>& valueId,
            const ReasonList& reasons) {
            publishTimeoutFeedback(replyTopic, cachedValue, valueId, reasons);
        },
        [this] {
            return collectConfiguredNodeIdsForPolling();
        })
    , unresponsiveInputTimeout_(unresponsiveInputTimeoutMs)
    , unresponsiveTimeoutErrorThreshold_(std::max<std::size_t>(1U, unresponsiveTimeoutErrorThreshold)) {
    polling_.start();
}

ZwaveController::~ZwaveController() {
    polling_.stop();
}

void ZwaveController::setPublishCallback(PublishCallback callback) {
    publishCallback_ = std::move(callback);
}

void ZwaveController::setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& devices) {
    std::scoped_lock lock{devicesMutex_};
    devices_ = devices;
    devicesMapper_ = ZwaveDevicesMapper{devices_};
}

void ZwaveController::setValue(const std::string& topic, const Value& value, const ReasonList& reasons) {
    const std::vector<std::string> topicParts = zwave_controller_topic_utils::splitTopic(topic);
    if (topicParts.size() < zwave_controller_topic_utils::kSetTopicMinimumParts) {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    if (topicParts.back() != "set") {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    const ZwaveNodeMap nodeMap = buildNodeMap();
    const std::string directTopic = zwave_controller_topic_utils::joinTopicParts(topicParts, topicParts.size() - 1U);

    std::string replyTopic = directTopic;
    ZwaveResolvedId target{};
    try {
        target = devicesMapper_.topicToZwaveId(nodeMap, directTopic, std::nullopt);
    } catch (...) {
        const std::optional<std::string> objectLabel = zwave_controller_topic_utils::parseOptionalLabelFromSetTopic(topicParts);
        const std::string deviceTopic = zwave_controller_topic_utils::joinTopicParts(
            topicParts,
            topicParts.size() - zwave_controller_topic_utils::kSetTopicMinimumParts);
        target = devicesMapper_.topicToZwaveId(nodeMap, deviceTopic, objectLabel);
        replyTopic = deviceTopic;
    }

    const ZwaveWriteRequest writeRequest = ZwaveDevicesMapper::buildWriteRequest(target, value);

    if (writeRequest.kind == ZwaveWriteKind::SetConfigParam) {
        driverPort_.setConfigParam(target.nodeId, target.index, std::get<double>(writeRequest.value));
        return;
    }

    driverPort_.setValue(target, writeRequest.value);
    polling_.rememberPendingCommand(
        replyTopic,
        writeRequest.target,
        zwave_controller_value_utils::toExpectedOutboundValue(
            zwave_controller_value_utils::writeValueToExpectedValue(writeRequest),
            writeRequest.target.type),
        reasons);
}

void ZwaveController::addDevice() {
    driverPort_.addNode();
}

void ZwaveController::removeFailedNode(const Value& value) {
    const std::optional<std::uint16_t> nodeId = zwave_controller_topic_utils::parseNodeIdFromValue(value);
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

void ZwaveController::requestNodeInfo(const Value& value) {
    const std::optional<std::uint16_t> nodeId = zwave_controller_topic_utils::parseNodeIdFromValue(value);
    if (!nodeId.has_value()) {
        throw std::runtime_error("requestnodeinfo requires numeric node id");
    }
    driverPort_.requestNodeInfo(*nodeId);
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

    polling_.removePendingCommandsForNode(nodeId);

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
        polling_.removeCachedTopics(topicsToErase);
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
        for (const std::uint16_t classId : zwave_controller_reason_utils::kEnablePollAllowedClasses) {
            driverPort_.enablePoll(nodeId, classId);
        }

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
        if (zwave_controller_reason_utils::isEnablePollAllowedClass(event.classId)) {
            driverPort_.enablePoll(event.nodeId, event.classId);
        }
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
            if (zwave_controller_reason_utils::isEnablePollAllowedClass(event.classId)) {
                driverPort_.enablePoll(event.nodeId, event.classId);
            }
        }
    }
    updateNodeHealthState(
        event.nodeId,
        NodeHealthState::Alive,
        "node " + std::to_string(event.nodeId) + " sent value information");

    publishValue(
        event.nodeId,
        event,
        zwave_controller_reason_utils::buildZwaveNetworkReason(event.nodeId, event.valueId));
    updateNodeCommState(
        event.nodeId,
        NodeCommState::Ok,
        zwave_controller_reason_utils::buildValueEventCommunicationReason(event, "openzwave_value_changed"));
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
        zwave_controller_reason_utils::buildValueEventCommunicationReason(event, "openzwave_value_refreshed"));
    clearNodeErrorState(event.nodeId, "node " + std::to_string(event.nodeId) + " communication recovered");

    publishValue(
        event.nodeId,
        event,
        zwave_controller_reason_utils::buildZwaveNetworkReason(event.nodeId, event.valueId));
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

void ZwaveController::publish(const std::string& topic, const Value& value, const std::string& reason) {
    publish(topic, value, reason, ReasonList{});
}

void ZwaveController::publish(
    const std::string& topic,
    const Value& value,
    const std::string& reason,
    const ReasonList& prependedReasons) {
    if (!publishCallback_) {
        return;
    }

    Message message{topic, value};
    message.addReason(reason);
    for (const auto& entry : prependedReasons | std::views::reverse) {
        zwave_controller_value_utils::addSpecCompliantReason(message, entry);
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
        ReasonList prependedReasons{};
        if (nodeId == kUsbControllerNodeId) {
            topic = usb_.topic;
        } else {
            const std::optional<ZwaveTopicMapping> mapping = devicesMapper_.valueToTopicAndType(buildDescriptor(event));
            if (!mapping.has_value() || mapping->topic.empty()) {
                throw std::runtime_error("missing topic mapping");
            }

            topic = mapping->topic;
            outputValue = zwave_controller_value_utils::applySwitchOutboundConversion(event.value, mapping->type);
            prependedReasons = polling_.takeMatchingPendingReasons(
                topic,
                event.nodeId,
                event.classId,
                event.instance,
                event.index,
                outputValue);
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
        + zwave_controller_reason_utils::buildZwaveNetworkReason(event.nodeId, event.valueId);

    publish(parameterTopic + "/supported", Value{std::string{"on"}}, reason);
    publish(parameterTopic + "/type", Value{event.type.empty() ? std::string{"unknown"} : event.type}, reason);
    publish(parameterTopic + "/read_only", Value{event.readOnly ? std::string{"on"} : std::string{"off"}}, reason);

    if (event.label.has_value() && !event.label->empty()) {
        publish(parameterTopic + "/label", Value{*event.label}, reason);
    }
}

std::string ZwaveController::describeTimeoutSource(const std::uint16_t nodeId) {
    return polling_.describeTimeoutSource(nodeId);
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
        cachedValue = zwave_controller_value_utils::applySwitchOutboundConversion(event.value, mapping->type);
    }

    polling_.cacheTopicState(topic, cachedValue, event.valueId);
}

std::vector<std::uint16_t> ZwaveController::collectConfiguredNodeIdsForPolling() const {
    std::vector<ZwaveDeviceConfig> deviceSnapshot{};
    {
        std::scoped_lock lock{devicesMutex_};
        deviceSnapshot = devices_;
    }

    std::vector<std::uint16_t> nodeIds{};
    nodeIds.reserve(deviceSnapshot.size());
    for (const auto& device : deviceSnapshot) {
        nodeIds.push_back(device.nodeId);
    }

    return nodeIds;
}

void ZwaveController::publishTimeoutFeedback(
    const std::string& replyTopic,
    const Value& cachedValue,
    const std::optional<std::uint64_t>& valueId,
    const ReasonList& reasons) {
    const std::string timeoutReason = valueId.has_value()
        ? "timeout waiting for zwave network id: " + std::to_string(*valueId)
        : "timeout waiting for zwave network id: unknown";

    publish(replyTopic, cachedValue, timeoutReason, reasons);
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
