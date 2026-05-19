#include "yaha/zwave/zwave_service_component.h"

#include <exception>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace yaha {

namespace {

[[nodiscard]] bool importantLogEnabled(const ZwaveConfig& config) {
    return config.logLevel >= 1U;
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
    Message error{"$MONITOR/zwave/error", std::string{operation + " failed"}};
    error.addReason("operation=" + operation);
    if (!detail.empty()) {
        error.addReason(detail);
    }
    return error;
}

[[nodiscard]] std::string valueToLogText(const Value& messageValue) {
    if (const auto* textValue = std::get_if<std::string>(&messageValue); textValue != nullptr) {
        return *textValue;
    }

    std::ostringstream stream{};
    stream << std::get<double>(messageValue);
    return stream.str();
}

[[nodiscard]] std::string qosToLogText(const Qos qosValue) {
    switch (qosValue) {
    case Qos::AtMostOnce:
        return "0";
    case Qos::AtLeastOnce:
        return "1";
    case Qos::ExactlyOnce:
        return "2";
    }

    return "?";
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

    Message infoMessage{"$MONITOR/zwave/info", std::string{"configuration reloaded"}};
    infoMessage.addReason("updated");
    publish(withPublishFlags(infoMessage, config_.qos, config_.retain));
}

SubscriptionMap ZwaveServiceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({"$MONITOR/zwave/removefailednode/set", Qos::ExactlyOnce});
    subscriptions.insert({"$MONITOR/zwave/addnode/set", Qos::ExactlyOnce});
    subscriptions.insert({"$MONITOR/zwave/scan/set", Qos::ExactlyOnce});

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

    if (isRemoveFailedTopic(message.topic())) {
        logImportantEvent("removefailednode", "request received");
        try {
            controller_->removeFailedNode(message.value());
            logImportantEvent("removefailednode", "request forwarded");
        } catch (const std::exception& exceptionValue) {
            logImportantError("removefailednode", exceptionValue.what());
            publish(withPublishFlags(makeOperationErrorMessage("removefailednode", exceptionValue.what()),
                                     config_.qos,
                                     config_.retain));
        } catch (...) {
            logImportantError("removefailednode", "unknown");
            publish(withPublishFlags(makeOperationErrorMessage("removefailednode", "unknown"),
                                     config_.qos,
                                     config_.retain));
        }
        return;
    }

    if (isAddNodeTopic(message.topic())) {
        logImportantEvent("addnode", "request received");
        try {
            controller_->addDevice();
            logImportantEvent("addnode", "request forwarded");
        } catch (const std::exception& exceptionValue) {
            logImportantError("addnode", exceptionValue.what());
            publish(withPublishFlags(makeOperationErrorMessage("addnode", exceptionValue.what()),
                                     config_.qos,
                                     config_.retain));
        } catch (...) {
            logImportantError("addnode", "unknown");
            publish(withPublishFlags(makeOperationErrorMessage("addnode", "unknown"),
                                     config_.qos,
                                     config_.retain));
        }
        return;
    }

    if (isScanTopic(message.topic())) {
        logImportantEvent("scan", "request received");
        try {
            controller_->startScan();
            logImportantEvent("scan", "request accepted");
            Message notification{"$MONITOR/zwave/notification", std::string{"scan command accepted"}};
            notification.addReason("scan command accepted by controller");
            publish(withPublishFlags(notification, config_.qos, config_.retain));
        } catch (const std::exception& exception) {
            logImportantError("scan", exception.what());
            Message error{"$MONITOR/zwave/error", std::string{"scan command failed"}};
            error.addReason(exception.what());
            publish(withPublishFlags(error, config_.qos, config_.retain));
        } catch (...) {
            logImportantError("scan", "unknown");
            Message error{"$MONITOR/zwave/error", std::string{"scan command failed"}};
            error.addReason("unknown");
            publish(withPublishFlags(error, config_.qos, config_.retain));
        }
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

    Message removeFailedRestart{"$MONITOR/zwave/removefailednode", std::string{"nop"}};
    removeFailedRestart.addReason("zwave restarted");
    publish(withPublishFlags(removeFailedRestart, config_.qos, config_.retain));

    Message addNodeRestart{"$MONITOR/zwave/addnode", std::string{"nop"}};
    addNodeRestart.addReason("zwave restarted");
    publish(withPublishFlags(addNodeRestart, config_.qos, config_.retain));

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

void ZwaveServiceComponent::handleControllerPublish(const Message& message) {
    logImportantEvent("controller_publish", "received");
    publish(withPublishFlags(message, config_.qos, config_.retain));
}

void ZwaveServiceComponent::logIncomingMessageIfEnabled(const Message& message) const {
    if (!config_.logIncomingMessages) {
        return;
    }

    std::cout << "zwave_service[in] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << '\n';
    for (const auto& entry : message.reason()) {
        std::cout << "  reason: [" << entry.timestamp << "] " << entry.message << '\n';
    }
    std::cout << std::flush;
}

void ZwaveServiceComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    if (!config_.logOutgoingMessages) {
        return;
    }

    std::cout << "zwave_service[out] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << '\n';
    for (const auto& entry : message.reason()) {
        std::cout << "  reason: [" << entry.timestamp << "] " << entry.message << '\n';
    }
    std::cout << std::flush;
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
    if (!publishCallback_) {
        std::cout << "zwave_service[error] op=publish reason=callback_missing"
                  << " topic=" << message.topic()
                  << '\n' << std::flush;
        return;
    }

    try {
        const PublishResult result = publishCallback_(message);
        if (!result.success) {
            std::cout << "zwave_service[error] op=publish reason=publish_rejected"
                      << " topic=" << message.topic()
                      << " category=" << static_cast<int>(result.category)
                      << " detail=\"" << result.reason << "\""
                      << '\n' << std::flush;
            return;
        }

        logOutgoingMessageIfEnabled(message);
    } catch (const std::exception& exceptionValue) {
        std::cout << "zwave_service[error] op=publish reason=exception"
                  << " topic=" << message.topic()
                  << " detail=\"" << exceptionValue.what() << "\""
                  << '\n' << std::flush;
    } catch (...) {
        std::cout << "zwave_service[error] op=publish reason=exception"
                  << " topic=" << message.topic()
                  << " detail=\"unknown\""
                  << '\n' << std::flush;
    }
}

bool ZwaveServiceComponent::isRemoveFailedTopic(const std::string& topic) {
    return topic == "$MONITOR/zwave/removefailednode/set";
}

bool ZwaveServiceComponent::isAddNodeTopic(const std::string& topic) {
    return topic == "$MONITOR/zwave/addnode/set";
}

bool ZwaveServiceComponent::isScanTopic(const std::string& topic) {
    return topic == "$MONITOR/zwave/scan/set";
}

} // namespace yaha
