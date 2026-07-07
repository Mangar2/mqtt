#include "yaha/zwave/zwave_service_component.h"
#include "json/json_value.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/message/message_log_service.h"

#include <exception>
#include <cctype>
#include <cmath>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

namespace {

constexpr std::string_view kSystemZwavePrefix = "system/zwave";
constexpr std::string_view kMonitorZwavePrefix = "$MONITOR/zwave";
constexpr double kNumericCommandTolerance = 1e-9;

[[nodiscard]] bool importantLogEnabled(const ZwaveConfig& config) {
    return config.logLevel >= 1U;
}

[[nodiscard]] std::string makeTopic(const std::string_view prefix, const std::string_view suffix) {
    return std::string{prefix} + "/" + std::string{suffix};
}

[[nodiscard]] std::optional<std::string> extractJsonStringField(const std::string& payloadText,
                                                                const std::string& fieldName) {
    const auto parsedValue = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedValue.has_value() || !parsedValue->is_object() || !parsedValue->contains(fieldName)) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& keyValue = parsedValue->at(fieldName);
    if (!keyValue.is_string()) {
        return std::nullopt;
    }

    return keyValue.as_string();
}

[[nodiscard]] Message withPublishFlags(const Message& input, const Qos qos, const bool retain) {
    Message output{input.topic(), input.value(), qos, retain, input.dup()};
    for (const auto& entry : input.reason() | std::views::reverse) {
        output.addReason(entry.message, entry.timestamp);
    }
    if (input.rawPayload().has_value()) {
        output.setRawPayload(*input.rawPayload());
    }
    return output;
}

[[nodiscard]] Message makeOperationErrorMessage(const std::string& operation,
                                                const std::string& detail) {
    Message error{makeTopic(kSystemZwavePrefix, "error"), std::string{operation + " failed"}};
    error.addReason("operation=" + operation);
    if (!detail.empty()) {
        error.addReason(detail);
    }
    return error;
}

[[nodiscard]] std::string toLower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

[[nodiscard]] std::optional<std::string> valueAsString(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<bool> parseStartStopCommand(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        if (std::fabs(*numericValue - 1.0) < kNumericCommandTolerance) {
            return true;
        }
        if (std::fabs(*numericValue) < kNumericCommandTolerance) {
            return false;
        }
        return std::nullopt;
    }

    const std::string normalized = toLower(std::get<std::string>(value));
    if (normalized == "on"
        || normalized == "true"
        || normalized == "1"
        || normalized == "start"
        || normalized == "now"
        || normalized == "enable"
        || normalized == "enabled") {
        return true;
    }

    if (normalized == "off"
        || normalized == "false"
        || normalized == "0"
        || normalized == "stop"
        || normalized == "cancel"
        || normalized == "disable"
        || normalized == "disabled") {
        return false;
    }

    return std::nullopt;
}

[[nodiscard]] std::string encodeKnownNodesJson(const std::vector<std::uint16_t>& nodeIds) {
    mqtt::json::JsonValue::Array nodesJsonArray{};
    nodesJsonArray.reserve(nodeIds.size());
    for (const std::uint16_t nodeId : nodeIds) {
        nodesJsonArray.emplace_back(static_cast<double>(nodeId));
    }

    mqtt::json::JsonValue::Object rootJsonObject{};
    rootJsonObject.emplace("nodes", mqtt::json::JsonValue{std::move(nodesJsonArray)});
    return mqtt::json::JsonValue{std::move(rootJsonObject)}.stringify();
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
    config_.devices = config;
    controller_->setDeviceConfiguration(config);

    Message infoMessage{makeTopic(kSystemZwavePrefix, "info"), std::string{"configuration reloaded"}};
    infoMessage.addReason("updated");
    publish(withPublishFlags(infoMessage, config_.qos, config_.retain));
}

SubscriptionMap ZwaveServiceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({makeTopic(kSystemZwavePrefix, "removefailednode/set"), Qos::ExactlyOnce});
    subscriptions.insert({makeTopic(kSystemZwavePrefix, "addnode/set"), Qos::ExactlyOnce});
    subscriptions.insert({makeTopic(kSystemZwavePrefix, "scan/set"), Qos::ExactlyOnce});
    subscriptions.insert({makeTopic(kSystemZwavePrefix, "requestnodeinfo/set"), Qos::ExactlyOnce});

    if (config_.fileStoreEnabled && !config_.fileStoreMonitorTopicPrefix.empty()) {
        subscriptions.insert({config_.fileStoreMonitorTopicPrefix + "/#", config_.subscribeQos});
    }

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
    logIncomingMessageIfEnabled(message);

    if (handleFileStoreMonitorReload(message)) {
        return;
    }

    if (isRemoveFailedTopic(message.topic())) {
        logImportantEvent("removefailednode", "request received");
        publishManagementStatus("removefailednode", message.value(), "removefailednode requested");
        try {
            controller_->removeFailedNode(message.value());
            logImportantEvent("removefailednode", "request forwarded");
            publishManagementStatus("removefailednode", Value{std::string{"deleted"}}, "removefailednode deleted");
        } catch (const std::exception& exceptionValue) {
            logImportantError("removefailednode", exceptionValue.what());
            publishManagementStatus("removefailednode", Value{0.0}, "removefailednode failed");
            publish(withPublishFlags(makeOperationErrorMessage("removefailednode", exceptionValue.what()),
                                     config_.qos,
                                     config_.retain));
        } catch (...) {
            logImportantError("removefailednode", "unknown");
            publishManagementStatus("removefailednode", Value{0.0}, "removefailednode failed with unknown error");
            publish(withPublishFlags(makeOperationErrorMessage("removefailednode", "unknown"),
                                     config_.qos,
                                     config_.retain));
        }
        return;
    }

    if (isAddNodeTopic(message.topic())) {
        logImportantEvent("addnode", "request received");
        const std::optional<bool> addNodeRequested = parseStartStopCommand(message.value());
        if (!addNodeRequested.has_value()) {
            logImportantError("addnode", "unsupported payload");
            addNodeActive_ = false;
            publishManagementStatus("addnode", Value{std::string{"off"}}, "addnode ignored: unsupported payload");
            publish(withPublishFlags(makeOperationErrorMessage("addnode", "unsupported payload"),
                                     config_.qos,
                                     config_.retain));
            return;
        }

        if (!*addNodeRequested) {
            addNodeActive_ = false;
            publishManagementStatus("addnode", Value{std::string{"off"}}, "addnode inclusion mode disabled");
            logImportantEvent("addnode", "request disabled");
            return;
        }

        addNodeActive_ = true;
        publishManagementStatus("addnode", Value{std::string{"on"}}, "addnode inclusion mode requested");
        try {
            controller_->addDevice();
            logImportantEvent("addnode", "request forwarded");
        } catch (const std::exception& exceptionValue) {
            logImportantError("addnode", exceptionValue.what());
            addNodeActive_ = false;
            publishManagementStatus("addnode", Value{std::string{"off"}}, "addnode failed");
            publish(withPublishFlags(makeOperationErrorMessage("addnode", exceptionValue.what()),
                                     config_.qos,
                                     config_.retain));
        } catch (...) {
            logImportantError("addnode", "unknown");
            addNodeActive_ = false;
            publishManagementStatus("addnode", Value{std::string{"off"}}, "addnode failed with unknown error");
            publish(withPublishFlags(makeOperationErrorMessage("addnode", "unknown"),
                                     config_.qos,
                                     config_.retain));
        }
        return;
    }

    if (isScanTopic(message.topic())) {
        logImportantEvent("scan", "request received");
        scanActive_ = true;
        publishManagementStatus("scan", Value{std::string{"on"}}, "scan mode requested");
        try {
            controller_->startScan();
            logImportantEvent("scan", "request accepted");
            Message notification{makeTopic(kSystemZwavePrefix, "notification"), std::string{"scan command accepted"}};
            notification.addReason("scan command accepted by controller");
            publish(withPublishFlags(notification, config_.qos, config_.retain));
        } catch (const std::exception& exception) {
            logImportantError("scan", exception.what());
            scanActive_ = false;
            publishManagementStatus("scan", Value{std::string{"off"}}, "scan mode ended due to error");
            Message error{makeTopic(kSystemZwavePrefix, "error"), std::string{"scan command failed"}};
            error.addReason(exception.what());
            publish(withPublishFlags(error, config_.qos, config_.retain));
        } catch (...) {
            logImportantError("scan", "unknown");
            scanActive_ = false;
            publishManagementStatus("scan", Value{std::string{"off"}}, "scan mode ended with unknown error");
            Message error{makeTopic(kSystemZwavePrefix, "error"), std::string{"scan command failed"}};
            error.addReason("unknown");
            publish(withPublishFlags(error, config_.qos, config_.retain));
        }
        return;
    }

    if (handleRequestNodeInfoCommand(message)) {
        return;
    }

    Message routedMessage{message.topic(), message.value(), message.qos(), message.retain(), message.dup()};
    routedMessage.addReason("received by zwave service");
    for (const auto& entry : message.reason() | std::views::reverse) {
        routedMessage.addReason(entry.message, entry.timestamp);
    }

    try {
        controller_->setValue(message.topic(), message.value(), routedMessage.reason());
        logImportantEvent("setvalue", "request forwarded");
    } catch (const std::exception& exceptionValue) {
        logImportantError("setvalue", exceptionValue.what());
        publish(withPublishFlags(makeOperationErrorMessage("setvalue", exceptionValue.what()),
                                 config_.qos,
                                 config_.retain));
    } catch (...) {
        logImportantError("setvalue", "unknown");
        publish(withPublishFlags(makeOperationErrorMessage("setvalue", "unknown"),
                                 config_.qos,
                                 config_.retain));
    }
}

void ZwaveServiceComponent::run() {
    logImportantEvent("run", "startup");

    addNodeActive_ = false;
    scanActive_ = false;
    publishManagementStatus("removefailednode", Value{0.0}, "zwave service restarted");
    publishManagementStatus("addnode", Value{std::string{"off"}}, "zwave service restarted");
    publishManagementStatus("scan", Value{std::string{"off"}}, "zwave service restarted");
    publishManagementStatus("requestnodeinfo", Value{std::string{"off"}}, "zwave service restarted");

    try {
        const std::vector<std::uint16_t> nodeIds = controller_->knownNodeIds();
        Message knownNodesStatus{makeTopic(kMonitorZwavePrefix, "nodes/known"), encodeKnownNodesJson(nodeIds)};
        knownNodesStatus.addReason("zwave known nodes snapshot on service startup");
        publish(withPublishFlags(knownNodesStatus, config_.qos, config_.retain));
        logImportantEvent("knownnodes", "startup snapshot published");
    } catch (const std::exception& exceptionValue) {
        logImportantError("knownnodes", exceptionValue.what());
    } catch (...) {
        logImportantError("knownnodes", "unknown");
    }

    try {
        controller_->requestConfigParametersForAllNodes();
        logImportantEvent("requestconfig", "requested for all nodes");
    } catch (const std::exception& exceptionValue) {
        logImportantError("requestconfig", exceptionValue.what());
        publish(withPublishFlags(makeOperationErrorMessage("requestconfig", exceptionValue.what()),
                                 config_.qos,
                                 config_.retain));
    } catch (...) {
        logImportantError("requestconfig", "unknown");
        publish(withPublishFlags(makeOperationErrorMessage("requestconfig", "unknown"),
                                 config_.qos,
                                 config_.retain));
    }
}

void ZwaveServiceComponent::close() {
    logImportantEvent("close", "shutdown requested");

    try {
        controller_->close();
        logImportantEvent("close", "shutdown complete");
    } catch (const std::exception& exceptionValue) {
        logImportantError("close", exceptionValue.what());
        publish(withPublishFlags(makeOperationErrorMessage("close", exceptionValue.what()),
                                 config_.qos,
                                 config_.retain));
    } catch (...) {
        logImportantError("close", "unknown");
        publish(withPublishFlags(makeOperationErrorMessage("close", "unknown"),
                                 config_.qos,
                                 config_.retain));
    }
}

void ZwaveServiceComponent::setPublishCallback(PublishCallback callback) {
    publishCallback_ = std::move(callback);
}

void ZwaveServiceComponent::setFileStoreReloadCallback(FileStoreReloadCallback callback) {
    fileStoreReloadCallback_ = std::move(callback);
}

void ZwaveServiceComponent::handleControllerPublish(const Message& message) {
    updateScanStatusFromControllerMessage(message);
    updateAddNodeStatusFromControllerMessage(message);
    logImportantEvent("controller_publish", "received");
    publish(withPublishFlags(message, config_.qos, config_.retain));
}

void ZwaveServiceComponent::publishManagementStatus(const std::string& topicSuffix,
                                                    const Value& value,
                                                    const std::string& reason) const {
    Message status{makeTopic(kSystemZwavePrefix, topicSuffix), value};
    status.addReason(reason);
    publish(withPublishFlags(status, config_.qos, config_.retain));
}

void ZwaveServiceComponent::updateScanStatusFromControllerMessage(const Message& message) {
    if (!scanActive_) {
        return;
    }

    const bool isNotificationTopic = message.topic() == makeTopic(kMonitorZwavePrefix, "notification")
        || message.topic() == makeTopic(kSystemZwavePrefix, "notification");
    if (!isNotificationTopic) {
        return;
    }

    const std::optional<std::string> value = valueAsString(message.value());
    if (!value.has_value()) {
        return;
    }

    const std::string normalizedValue = toLower(*value);
    const bool scanCompleted = normalizedValue == "scan complete"
        || normalizedValue.find("scan completed") != std::string::npos
        || normalizedValue.find("scan done") != std::string::npos;
    if (!scanCompleted) {
        return;
    }

    scanActive_ = false;
    publishManagementStatus("scan", Value{std::string{"off"}}, "scan completed (controller feedback)");
}

void ZwaveServiceComponent::updateAddNodeStatusFromControllerMessage(const Message& message) {
    if (!addNodeActive_) {
        return;
    }

    const bool isNotificationTopic = message.topic() == makeTopic(kMonitorZwavePrefix, "notification")
        || message.topic() == makeTopic(kSystemZwavePrefix, "notification");
    if (!isNotificationTopic) {
        return;
    }

    const std::optional<std::string> value = valueAsString(message.value());
    if (!value.has_value()) {
        return;
    }

    const std::string normalizedValue = toLower(*value);
    const bool inclusionFinished = normalizedValue.find("done") != std::string::npos
        || normalizedValue.find("complete") != std::string::npos
        || normalizedValue.find("failed") != std::string::npos
        || normalizedValue.find("cancel") != std::string::npos
        || normalizedValue.find("timeout") != std::string::npos;
    if (!inclusionFinished) {
        return;
    }

    addNodeActive_ = false;
    publishManagementStatus("addnode", Value{std::string{"off"}}, "addnode mode ended (controller feedback)");
}

bool ZwaveServiceComponent::handleRequestNodeInfoCommand(const Message& message) {
    if (!isRequestNodeInfoTopic(message.topic())) {
        return false;
    }

    logImportantEvent("requestnodeinfo", "request received");
    try {
        controller_->requestNodeInfo(message.value());
        logImportantEvent("requestnodeinfo", "request forwarded");
    } catch (const std::exception& exceptionValue) {
        logImportantError("requestnodeinfo", exceptionValue.what());
        publish(withPublishFlags(makeOperationErrorMessage("requestnodeinfo", exceptionValue.what()),
                                 config_.qos,
                                 config_.retain));
    } catch (...) {
        logImportantError("requestnodeinfo", "unknown");
        publish(withPublishFlags(makeOperationErrorMessage("requestnodeinfo", "unknown"),
                                 config_.qos,
                                 config_.retain));
    }

    return true;
}

void ZwaveServiceComponent::logIncomingMessageIfEnabled(const Message& message) const {
    if (!config_.logIncomingMessages) {
        return;
    }

    const MessageLogConfig logConfig{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};
    const std::optional<std::string> logLine = buildMessageLogLine(
        "zwave_service",
        MessageLogDirection::Incoming,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void ZwaveServiceComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    if (!config_.logOutgoingMessages) {
        return;
    }

    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};
    const std::optional<std::string> logLine = buildMessageLogLine(
        "zwave_service",
        MessageLogDirection::Outgoing,
        message,
        logConfig);
    if (!logLine.has_value()) {
        return;
    }

    std::cout << *logLine << '\n' << std::flush;
}

void ZwaveServiceComponent::logImportantEvent(const std::string_view operation, const std::string_view detail) const {
    if (!importantLogEnabled(config_)) {
        return;
    }

    std::cout << "zwave_service[event] op=" << operation
              << " detail=\"" << detail << "\""
              << '\n' << std::flush;
}

void ZwaveServiceComponent::logImportantError(const std::string_view operation, const std::string_view detail) const {
    if (!importantLogEnabled(config_)) {
        return;
    }

    std::cout << "zwave_service[error] op=" << operation
              << " detail=\"" << detail << "\""
              << '\n' << std::flush;
}

void ZwaveServiceComponent::publish(const Message& message) const {
    const auto logPublishFailure = [&message](const std::string_view reason,
                                              const std::string_view detail,
                                              const std::optional<int> category) {
        const MessageLogConfig logConfig{
            .enableIncoming = false,
            .enableOutgoing = true,
            .includeReasonChain = true,
            .incomingTopicFilter = std::nullopt,
            .outgoingTopicFilter = std::nullopt};
        const std::optional<std::string> logLine = buildMessageLogLine(
            "zwave_service",
            MessageLogDirection::Outgoing,
            message,
            logConfig);
        if (!logLine.has_value()) {
            return;
        }

        std::cout << *logLine
                  << " event=publish_failed"
                  << " reason=" << reason;
        if (category.has_value()) {
            std::cout << " category=" << *category;
        }
        if (!detail.empty()) {
            std::cout << " detail=\"" << escapeJsonString(detail) << '\"';
        }
        std::cout << '\n' << std::flush;
    };

    if (!publishCallback_) {
        logPublishFailure("callback_missing", "", std::nullopt);
        return;
    }

    try {
        const PublishResult result = publishCallback_(message);
        if (!result.success) {
            logPublishFailure("publish_rejected", result.reason, static_cast<int>(result.category));
            return;
        }

        logOutgoingMessageIfEnabled(message);
    } catch (const std::exception& exceptionValue) {
        logPublishFailure("exception", exceptionValue.what(), std::nullopt);
    } catch (...) {
        logPublishFailure("exception", "unknown", std::nullopt);
    }
}

bool ZwaveServiceComponent::isRemoveFailedTopic(const std::string& topic) {
    return topic == makeTopic(kSystemZwavePrefix, "removefailednode/set");
}

bool ZwaveServiceComponent::isAddNodeTopic(const std::string& topic) {
    return topic == makeTopic(kSystemZwavePrefix, "addnode/set");
}

bool ZwaveServiceComponent::isScanTopic(const std::string& topic) {
    return topic == makeTopic(kSystemZwavePrefix, "scan/set");
}

bool ZwaveServiceComponent::isRequestNodeInfoTopic(const std::string& topic) {
    return topic == makeTopic(kSystemZwavePrefix, "requestnodeinfo/set");
}

bool ZwaveServiceComponent::handleFileStoreMonitorReload(const Message& message) {
    if (!config_.fileStoreEnabled || config_.fileStoreMonitorTopicPrefix.empty()) {
        return false;
    }

    const std::string monitorTopicPrefix = config_.fileStoreMonitorTopicPrefix + "/";
    if (!message.topic().starts_with(monitorTopicPrefix)) {
        return false;
    }

    const auto* payloadText = std::get_if<std::string>(&message.value());
    if (payloadText == nullptr) {
        return true;
    }

    const std::optional<std::string> keyPath = extractJsonStringField(*payloadText, "keyPath");
    if (!keyPath.has_value() || *keyPath != config_.settingsKeyPath) {
        return true;
    }

    if (!fileStoreReloadCallback_) {
        logImportantError("filestore_reload", "missing reload callback");
        return true;
    }

    std::vector<ZwaveDeviceConfig> loadedDevices{};
    std::string errorMessage{};
    if (!fileStoreReloadCallback_(loadedDevices, errorMessage)) {
        const std::string detail = errorMessage.empty() ? "reload callback failed" : errorMessage;
        logImportantError("filestore_reload", detail);
        return true;
    }

    setDeviceConfiguration(loadedDevices);
    logImportantEvent("filestore_reload", "applied device configuration update");
    return true;
}

} // namespace yaha
