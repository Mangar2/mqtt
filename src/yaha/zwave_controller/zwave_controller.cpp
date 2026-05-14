#include "yaha/zwave_controller/zwave_controller.h"

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr std::size_t kSetTopicMinimumParts = 2U;
constexpr std::uint16_t kUsbControllerNodeId = 1U;
constexpr double kIntegerTolerance = 1e-9;

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

[[nodiscard]] double valueAsDouble(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        return *numericValue;
    }

    std::size_t parsedChars = 0U;
    const std::string& text = std::get<std::string>(value);
    const double parsed = std::stod(text, &parsedChars);
    if (parsedChars != text.size()) {
        throw std::runtime_error("invalid numeric value '" + text + "'");
    }
    return parsed;
}

} // namespace

ZwaveController::ZwaveController(ZwaveUsbConfig usbConfig, IZwaveDriverPort& driverPort)
    : usb_(std::move(usbConfig))
    , driverPort_(driverPort)
    , devicesMapper_(std::vector<ZwaveDeviceConfig>{}) {
}

void ZwaveController::setPublishCallback(PublishCallback callback) {
    publishCallback_ = std::move(callback);
}

void ZwaveController::setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& devices) {
    devices_ = devices;
    devicesMapper_ = ZwaveDevicesMapper{devices_};
}

void ZwaveController::setValue(const std::string& topic, const Value& value) {
    const std::vector<std::string> topicParts = splitTopic(topic);
    if (topicParts.size() < kSetTopicMinimumParts) {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    if (topicParts.back() != "set") {
        throw std::runtime_error("set expected as last element in topic " + topic);
    }

    const std::optional<std::string> objectLabel = parseOptionalLabelFromSetTopic(topicParts);
    const std::string deviceTopic = joinTopicParts(topicParts, topicParts.size() - kSetTopicMinimumParts);

    const ZwaveResolvedId target = devicesMapper_.topicToZwaveId(buildNodeMap(), deviceTopic, objectLabel);
    const ZwaveWriteRequest writeRequest = ZwaveDevicesMapper::buildWriteRequest(target, value);

    if (writeRequest.kind == ZwaveWriteKind::SetConfigParam) {
        driverPort_.setConfigParam(target.nodeId, target.index, std::get<double>(writeRequest.value));
        return;
    }

    driverPort_.setValue(target, writeRequest.value);
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

    for (const auto& [classId, values] : nodeIterator->second.classes) {
        (void)values;
        if (classId == kZwaveSwitchBinaryClass || classId == kZwaveSwitchMultilevelClass) {
            driverPort_.enablePoll(nodeId, classId);
        }
    }
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
    (void)event;
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
    if (!publishCallback_) {
        return;
    }

    Message message{topic, value};
    message.addReason(reason);
    publishCallback_(message);
}

void ZwaveController::publishValue(
    const std::uint16_t nodeId,
    const ZwaveControllerValueEvent& event,
    std::string reason) {
    try {
        std::string topic{};
        Value outputValue = event.value;
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
        }

        publish(topic, outputValue, reason);
    } catch (...) {
        publish("$MONITOR/zwave/" + std::to_string(nodeId), event.value, reason);
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
