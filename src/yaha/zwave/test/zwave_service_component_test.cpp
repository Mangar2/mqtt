#include <catch2/catch_test_macros.hpp>

#include "yaha/zwave/zwave_service_component.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kNodeIdSeven = 7U;
constexpr std::uint16_t kNodeIdNine = 9U;
constexpr std::uint16_t kSwitchClass = 0x25U;
constexpr double kRemoveFailedPayload = 7.0;
constexpr int kUnknownThrowSetValue = 1;
constexpr int kUnknownThrowAddNode = 2;
constexpr int kUnknownThrowRemoveFailed = 3;
constexpr int kUnknownThrowRequestConfig = 4;
constexpr int kUnknownThrowClose = 5;
constexpr int kUnknownThrowPublish = 99;

class FakeController final : public yaha::IZwaveController {
public:
    void setPublishCallback(yaha::PublishCallback callback) override {
        callbackFromService_ = std::move(callback);
    }

    void setDeviceConfiguration(const std::vector<yaha::ZwaveDeviceConfig>& devices) override {
        configuredDevices_ = devices;
    }

    void setValue(const std::string& topic, const yaha::Value& value, const std::vector<yaha::ReasonEntry>& reasons) override {
        if (throwUnknownOnSetValue_) {
            throw kUnknownThrowSetValue;
        }
        if (throwOnSetValue_) {
            throw std::runtime_error{"setvalue failed in fake controller"};
        }
        setValueCalls_ += 1U;
        lastSetTopic_ = topic;
        lastSetValue_ = value;
        lastSetReasons_ = reasons;
    }

    void addDevice() override {
        if (throwUnknownOnAddDevice_) {
            throw kUnknownThrowAddNode;
        }
        if (throwOnAddDevice_) {
            throw std::runtime_error{"addnode failed in fake controller"};
        }
        addDeviceCalls_ += 1U;
    }

    void removeFailedNode(const yaha::Value& value) override {
        if (throwUnknownOnRemoveFailed_) {
            throw kUnknownThrowRemoveFailed;
        }
        if (throwOnRemoveFailed_) {
            throw std::runtime_error{"removefailednode failed in fake controller"};
        }
        removeFailedCalls_ += 1U;
        lastRemoveFailedValue_ = value;
    }

    void startScan() override {
        if (throwUnknownOnScan_) {
            throw kUnknownThrowClose;
        }
        startScanCalls_ += 1U;
        if (throwOnScan_) {
            throw std::runtime_error{"scan failed in fake controller"};
        }
    }

    void requestConfigParametersForAllNodes() override {
        if (throwUnknownOnRequestConfig_) {
            throw kUnknownThrowRequestConfig;
        }
        if (throwOnRequestConfig_) {
            throw std::runtime_error{"requestconfig failed in fake controller"};
        }
        requestConfigCalls_ += 1U;
    }

    [[nodiscard]] std::vector<std::uint16_t> knownNodeIds() const override {
        return knownNodeIds_;
    }

    void close() override {
        if (throwUnknownOnClose_) {
            throw kUnknownThrowClose;
        }
        if (throwOnClose_) {
            throw std::runtime_error{"close failed in fake controller"};
        }
        closeCalls_ += 1U;
    }

    void emitControllerPublish(const yaha::Message& message) const {
        if (callbackFromService_) {
            callbackFromService_(message);
        }
    }

    void setThrowOnScan(const bool enabled) {
        throwOnScan_ = enabled;
    }

    void setThrowUnknownOnScan(const bool enabled) {
        throwUnknownOnScan_ = enabled;
    }

    void setThrowOnSetValue(const bool enabled) {
        throwOnSetValue_ = enabled;
    }

    void setThrowUnknownOnSetValue(const bool enabled) {
        throwUnknownOnSetValue_ = enabled;
    }

    void setThrowOnAddDevice(const bool enabled) {
        throwOnAddDevice_ = enabled;
    }

    void setThrowUnknownOnAddDevice(const bool enabled) {
        throwUnknownOnAddDevice_ = enabled;
    }

    void setThrowOnRemoveFailed(const bool enabled) {
        throwOnRemoveFailed_ = enabled;
    }

    void setThrowUnknownOnRemoveFailed(const bool enabled) {
        throwUnknownOnRemoveFailed_ = enabled;
    }

    void setThrowOnRequestConfig(const bool enabled) {
        throwOnRequestConfig_ = enabled;
    }

    void setThrowUnknownOnRequestConfig(const bool enabled) {
        throwUnknownOnRequestConfig_ = enabled;
    }

    void setThrowOnClose(const bool enabled) {
        throwOnClose_ = enabled;
    }

    void setThrowUnknownOnClose(const bool enabled) {
        throwUnknownOnClose_ = enabled;
    }

    void setKnownNodeIds(std::vector<std::uint16_t> nodeIds) {
        knownNodeIds_ = std::move(nodeIds);
    }

    [[nodiscard]] std::size_t setValueCalls() const {
        return setValueCalls_;
    }

    [[nodiscard]] const std::string& lastSetTopic() const {
        return lastSetTopic_;
    }

    [[nodiscard]] const yaha::Value& lastSetValue() const {
        return lastSetValue_;
    }

    [[nodiscard]] const std::vector<yaha::ReasonEntry>& lastSetReasons() const {
        return lastSetReasons_;
    }

    [[nodiscard]] std::size_t addDeviceCalls() const {
        return addDeviceCalls_;
    }

    [[nodiscard]] std::size_t removeFailedCalls() const {
        return removeFailedCalls_;
    }

    [[nodiscard]] const yaha::Value& lastRemoveFailedValue() const {
        return lastRemoveFailedValue_;
    }

    [[nodiscard]] std::size_t startScanCalls() const {
        return startScanCalls_;
    }

    [[nodiscard]] std::size_t requestConfigCalls() const {
        return requestConfigCalls_;
    }

    [[nodiscard]] std::size_t closeCalls() const {
        return closeCalls_;
    }

private:
    yaha::PublishCallback callbackFromService_{};

    std::size_t setValueCalls_{0U};
    std::string lastSetTopic_{};
    yaha::Value lastSetValue_{std::string{}};
    std::vector<yaha::ReasonEntry> lastSetReasons_{};

    std::size_t addDeviceCalls_{0U};
    std::size_t removeFailedCalls_{0U};
    yaha::Value lastRemoveFailedValue_{0.0};
    std::size_t startScanCalls_{0U};
    std::size_t requestConfigCalls_{0U};
    std::size_t closeCalls_{0U};

    std::vector<yaha::ZwaveDeviceConfig> configuredDevices_{};
    bool throwOnScan_{false};
    bool throwUnknownOnScan_{false};
    bool throwOnSetValue_{false};
    bool throwOnAddDevice_{false};
    bool throwOnRemoveFailed_{false};
    bool throwOnRequestConfig_{false};
    bool throwOnClose_{false};
    bool throwUnknownOnSetValue_{false};
    bool throwUnknownOnAddDevice_{false};
    bool throwUnknownOnRemoveFailed_{false};
    bool throwUnknownOnRequestConfig_{false};
    bool throwUnknownOnClose_{false};
    std::vector<std::uint16_t> knownNodeIds_{};
};

[[nodiscard]] yaha::ZwaveConfig makeConfig() {
    yaha::ZwaveConfig config{};
    config.subscribeQos = yaha::Qos::AtMostOnce;
    config.qos = yaha::Qos::ExactlyOnce;
    config.retain = true;
    config.usb.device = "/dev/ttyUSB0";
    config.usb.topic = "controller/topic";

    yaha::ZwaveDeviceConfig classBound{};
    classBound.topic = "home/lamp";
    classBound.nodeId = kNodeIdSeven;
    classBound.classId = kSwitchClass;
    classBound.instance = 1U;
    classBound.index = 0U;
    classBound.type = std::string{"switch"};

    yaha::ZwaveDeviceConfig classFree{};
    classFree.topic = "home/climate";
    classFree.nodeId = kNodeIdNine;
    classFree.classId = std::nullopt;

    config.devices = {classBound, classFree};
    return config;
}

[[nodiscard]] bool hasReasonMessage(const yaha::Message& message, const std::string& reasonText) {
    return std::any_of(message.reason().begin(), message.reason().end(), [&](const yaha::ReasonEntry& entry) {
        return entry.message == reasonText;
    });
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("subscriptions_include_management_and_device_topics", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    const yaha::SubscriptionMap subscriptions = service.getSubscriptions();

    REQUIRE(subscriptions.contains("system/zwave/removefailednode/set"));
    REQUIRE(subscriptions.contains("system/zwave/addnode/set"));
    REQUIRE(subscriptions.contains("system/zwave/scan/set"));
    CHECK(subscriptions.at("system/zwave/removefailednode/set") == yaha::Qos::ExactlyOnce);
    CHECK(subscriptions.at("system/zwave/addnode/set") == yaha::Qos::ExactlyOnce);
    CHECK(subscriptions.at("system/zwave/scan/set") == yaha::Qos::ExactlyOnce);

    REQUIRE(subscriptions.contains("home/lamp/set"));
    REQUIRE(subscriptions.contains("home/climate/+/set"));
    CHECK(subscriptions.at("home/lamp/set") == yaha::Qos::AtMostOnce);
    CHECK(subscriptions.at("home/climate/+/set") == yaha::Qos::AtMostOnce);
}

    TEST_CASE("set_device_configuration_replaces_subscription_topics", "[zwave_service]") {
        auto controller = std::make_shared<FakeController>();
        yaha::ZwaveServiceComponent service{makeConfig(), controller};

        yaha::ZwaveDeviceConfig replacement{};
        replacement.topic = "home/reloaded";
        replacement.nodeId = kNodeIdSeven;
        replacement.classId = kSwitchClass;
        replacement.instance = 1U;
        replacement.index = 0U;
        replacement.type = std::string{"switch"};

        service.setDeviceConfiguration({replacement});

        const yaha::SubscriptionMap subscriptions = service.getSubscriptions();
        CHECK(subscriptions.contains("home/reloaded/set"));
        CHECK_FALSE(subscriptions.contains("home/lamp/set"));
        CHECK_FALSE(subscriptions.contains("home/climate/+/set"));
    }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("management_messages_are_forwarded_and_scan_success_is_published", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});
    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"on"}}});
    service.handleMessage(yaha::Message{"system/zwave/scan/set", yaha::Value{std::string{"now"}}});

    CHECK(controller->removeFailedCalls() == 1U);
    CHECK(controller->addDeviceCalls() == 1U);
    CHECK(controller->startScanCalls() == 1U);
    REQUIRE(std::holds_alternative<double>(controller->lastRemoveFailedValue()));
    CHECK(std::get<double>(controller->lastRemoveFailedValue()) == kRemoveFailedPayload);

    REQUIRE(published.size() == 5U);
    CHECK(published[0].topic() == "system/zwave/removefailednode");
    CHECK(published[1].topic() == "system/zwave/removefailednode");
    CHECK(published[2].topic() == "system/zwave/addnode");
    CHECK(published[3].topic() == "system/zwave/scan");
    CHECK(published[4].topic() == "system/zwave/notification");

    REQUIRE(std::holds_alternative<double>(published[0].value()));
    CHECK(std::get<double>(published[0].value()) == kRemoveFailedPayload);
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[1].value()) == "deleted");
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<std::string>(published[2].value()) == "on");
    REQUIRE(std::holds_alternative<std::string>(published[3].value()));
    CHECK(std::get<std::string>(published[3].value()) == "on");
    REQUIRE(std::holds_alternative<std::string>(published[4].value()));
    CHECK(std::get<std::string>(published[4].value()) == "scan command accepted");
    CHECK(hasReasonMessage(published[0], "removefailednode requested"));
    CHECK(hasReasonMessage(published[1], "removefailednode deleted"));
    CHECK(hasReasonMessage(published[2], "addnode inclusion mode requested"));
    CHECK(hasReasonMessage(published[3], "scan mode requested"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("scan_failure_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnScan(true);

    yaha::ZwaveServiceComponent service{makeConfig(), controller};
    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/scan/set", yaha::Value{std::string{"now"}}});

    REQUIRE(controller->startScanCalls() == 1U);
    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/scan");
    CHECK(published[1].topic() == "system/zwave/scan");
    CHECK(published[2].topic() == "system/zwave/error");
    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[0].value()) == "on");
    CHECK(std::get<std::string>(published[1].value()) == "off");
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<std::string>(published[2].value()) == "scan command failed");
    CHECK(hasReasonMessage(published[2], "scan failed in fake controller"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("scan_unknown_failure_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnScan(true);

    yaha::ZwaveServiceComponent service{makeConfig(), controller};
    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/scan/set", yaha::Value{std::string{"now"}}});

    REQUIRE(controller->startScanCalls() == 0U);
    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/scan");
    CHECK(published[1].topic() == "system/zwave/scan");
    CHECK(published[2].topic() == "system/zwave/error");
    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[0].value()) == "on");
    CHECK(std::get<std::string>(published[1].value()) == "off");
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<std::string>(published[2].value()) == "scan command failed");
    CHECK(hasReasonMessage(published[2], "unknown"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("scan_status_turns_off_when_controller_reports_scan_complete", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/scan/set", yaha::Value{std::string{"now"}}});
    controller->emitControllerPublish(yaha::Message{"$MONITOR/zwave/notification", yaha::Value{std::string{"scan complete"}}});

    REQUIRE(published.size() == 4U);
    CHECK(published[0].topic() == "system/zwave/scan");
    CHECK(published[1].topic() == "system/zwave/notification");
    CHECK(published[2].topic() == "system/zwave/scan");
    CHECK(published[3].topic() == "$MONITOR/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<std::string>(published[2].value()) == "off");
    CHECK(hasReasonMessage(published[2], "scan completed (controller feedback)"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("addnode_status_turns_off_when_controller_reports_completion", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"start"}}});
    controller->emitControllerPublish(yaha::Message{"$MONITOR/zwave/notification", yaha::Value{std::string{"done"}}});

    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/addnode");
    CHECK(published[1].topic() == "system/zwave/addnode");
    CHECK(published[2].topic() == "$MONITOR/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[0].value()) == "on");
    CHECK(std::get<std::string>(published[1].value()) == "off");
    CHECK(hasReasonMessage(published[1], "addnode mode ended (controller feedback)"));
}

TEST_CASE("addnode_off_command_disables_without_forwarding_to_controller", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"off"}}});

    CHECK(controller->addDeviceCalls() == 0U);
    REQUIRE(published.size() == 1U);
    CHECK(published[0].topic() == "system/zwave/addnode");
    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    CHECK(std::get<std::string>(published[0].value()) == "off");
    CHECK(hasReasonMessage(published[0], "addnode inclusion mode disabled"));
}

TEST_CASE("publish_without_callback_logs_error", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());
    service.run();
    std::cout.rdbuf(previousBuffer);

        REQUIRE(outputStream.str().find("component=\"zwave_service\" direction=\"outgoing\"")
            != std::string::npos);
        REQUIRE(outputStream.str().find("event=publish_failed reason=callback_missing")
            != std::string::npos);
}

TEST_CASE("publish_failure_result_logs_error", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    service.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::fail(yaha::PublishFailureCategory::AckTimeout, "no_ack");
    });

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());
    service.run();
    std::cout.rdbuf(previousBuffer);

        REQUIRE(outputStream.str().find("component=\"zwave_service\" direction=\"outgoing\"")
            != std::string::npos);
        REQUIRE(outputStream.str().find("event=publish_failed reason=publish_rejected")
            != std::string::npos);
    REQUIRE(outputStream.str().find("category=2") != std::string::npos);
    REQUIRE(outputStream.str().find("detail=\"no_ack\"") != std::string::npos);
}

TEST_CASE("publish_callback_exception_logs_error", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    service.setPublishCallback([](const yaha::Message&) {
        throw std::runtime_error{"publish failed in fake callback"};
    });

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());
    service.run();
    std::cout.rdbuf(previousBuffer);

        REQUIRE(outputStream.str().find("component=\"zwave_service\" direction=\"outgoing\"")
            != std::string::npos);
        REQUIRE(outputStream.str().find("event=publish_failed reason=exception")
            != std::string::npos);
    REQUIRE(outputStream.str().find("detail=\"publish failed in fake callback\"")
            != std::string::npos);
}

TEST_CASE("publish_callback_unknown_exception_logs_error", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    service.setPublishCallback([](const yaha::Message&) {
        throw kUnknownThrowPublish;
    });

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());
    service.run();
    std::cout.rdbuf(previousBuffer);

        REQUIRE(outputStream.str().find("component=\"zwave_service\" direction=\"outgoing\"")
            != std::string::npos);
        REQUIRE(outputStream.str().find("event=publish_failed reason=exception")
            != std::string::npos);
    REQUIRE(outputStream.str().find("detail=\"unknown\"") != std::string::npos);
}

TEST_CASE("logging_flags_emit_incoming_and_outgoing_lines", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveConfig config = makeConfig();
    config.logIncomingMessages = true;
    config.logOutgoingMessages = true;

    yaha::ZwaveServiceComponent service{config, controller};
    service.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());

    service.handleMessage(yaha::Message{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}});
    controller->emitControllerPublish(yaha::Message{"home/phase6/lamp", yaha::Value{std::string{"on"}}});

    std::cout.rdbuf(previousBuffer);

    const std::string logText = outputStream.str();
        REQUIRE(logText.find("component=\"zwave_service\" direction=\"incoming\" topic=\"home/phase6/lamp/set\"")
            != std::string::npos);
        REQUIRE(logText.find("component=\"zwave_service\" direction=\"outgoing\" topic=\"home/phase6/lamp\"")
            != std::string::npos);
}

TEST_CASE("log_level_one_emits_important_events_without_forcing_message_traces", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveConfig config = makeConfig();
    config.logLevel = 1U;
    config.logIncomingMessages = false;
    config.logOutgoingMessages = false;

    yaha::ZwaveServiceComponent service{config, controller};
    service.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());

    service.run();
    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"now"}}});

    std::cout.rdbuf(previousBuffer);

    const std::string logText = outputStream.str();
    REQUIRE(logText.find("zwave_service[event] op=run detail=\"startup\"") != std::string::npos);
    REQUIRE(logText.find("zwave_service[event] op=addnode detail=\"request received\"") != std::string::npos);
    REQUIRE(logText.find("zwave_service[event] op=addnode detail=\"request forwarded\"") != std::string::npos);
    CHECK(logText.find("component=\"zwave_service\" direction=\"incoming\"") == std::string::npos);
    CHECK(logText.find("component=\"zwave_service\" direction=\"outgoing\"") == std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("regular_set_message_forwards_reasons_and_publish_flags", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message incoming{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}};
    service.handleMessage(incoming);

    CHECK(controller->setValueCalls() == 1U);
    CHECK(controller->lastSetTopic() == "home/phase6/lamp/set");
    REQUIRE(std::holds_alternative<std::string>(controller->lastSetValue()));
    CHECK(std::get<std::string>(controller->lastSetValue()) == "on");
    REQUIRE_FALSE(controller->lastSetReasons().empty());
    CHECK(controller->lastSetReasons().back().message == "received by zwave service");

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{std::string{"on"}}};
    controllerPublish.addReason("device feedback");
    controller->emitControllerPublish(controllerPublish);

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "home/phase6/lamp");
    CHECK(published.front().qos() == yaha::Qos::ExactlyOnce);
    CHECK(published.front().retain());
    CHECK(hasReasonMessage(published.front(), "device feedback"));
    CHECK_FALSE(hasReasonMessage(published.front(), "received by zwave service"));
}

TEST_CASE("regular_set_message_value_mismatch_skips_reason_merge", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message incoming{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}};
    service.handleMessage(incoming);

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{std::string{"off"}}};
    controllerPublish.addReason("device feedback", "2026-05-13T10:00:00Z");
    controller->emitControllerPublish(controllerPublish);

    REQUIRE(published.size() == 1U);
    CHECK(hasReasonMessage(published.front(), "device feedback"));
    CHECK_FALSE(hasReasonMessage(published.front(), "received by zwave service"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("regular_set_message_type_mismatch_does_not_drop_publish", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message incoming{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}};
    service.handleMessage(incoming);

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{1.0}};
    controllerPublish.addReason("device feedback");
    REQUIRE_NOTHROW(controller->emitControllerPublish(controllerPublish));

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "home/phase6/lamp");
    REQUIRE(std::holds_alternative<double>(published.front().value()));
    CHECK(std::get<double>(published.front().value()) == 1.0);
    CHECK(hasReasonMessage(published.front(), "device feedback"));
    CHECK_FALSE(hasReasonMessage(published.front(), "received by zwave service"));
}

TEST_CASE("regular_set_message_invalid_received_timestamp_skips_reason_merge", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message incoming{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}};
    incoming.addReason("ui", "invalid-time");
    service.handleMessage(incoming);

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{std::string{"on"}}};
    controllerPublish.addReason("device feedback", "2026-05-13T10:00:00Z");
    controller->emitControllerPublish(controllerPublish);

    REQUIRE(published.size() == 1U);
    CHECK(hasReasonMessage(published.front(), "device feedback"));
    CHECK_FALSE(hasReasonMessage(published.front(), "received by zwave service"));
}

TEST_CASE("regular_set_message_out_of_window_timestamp_skips_reason_merge", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message incoming{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}};
    incoming.addReason("ui", "2026-05-13T10:00:00Z");
    service.handleMessage(incoming);

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{std::string{"on"}}};
    controllerPublish.addReason("device feedback", "2026-05-13T10:01:00Z");
    controller->emitControllerPublish(controllerPublish);

    REQUIRE(published.size() == 1U);
    CHECK(hasReasonMessage(published.front(), "device feedback"));
    CHECK_FALSE(hasReasonMessage(published.front(), "received by zwave service"));
}

TEST_CASE("remove_failed_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnRemoveFailed(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});

    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/removefailednode");
    CHECK(published[1].topic() == "system/zwave/removefailednode");
    CHECK(published[2].topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published[2], "operation=removefailednode"));
}

TEST_CASE("remove_failed_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnRemoveFailed(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});

    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/removefailednode");
    CHECK(published[1].topic() == "system/zwave/removefailednode");
    CHECK(published[2].topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published[2], "operation=removefailednode"));
    CHECK(hasReasonMessage(published[2], "unknown"));
}

TEST_CASE("add_node_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnAddDevice(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"on"}}});

    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/addnode");
    CHECK(published[1].topic() == "system/zwave/addnode");
    CHECK(published[2].topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published[2], "operation=addnode"));
}

TEST_CASE("add_node_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnAddDevice(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"system/zwave/addnode/set", yaha::Value{std::string{"on"}}});

    REQUIRE(published.size() == 3U);
    CHECK(published[0].topic() == "system/zwave/addnode");
    CHECK(published[1].topic() == "system/zwave/addnode");
    CHECK(published[2].topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published[2], "operation=addnode"));
    CHECK(hasReasonMessage(published[2], "unknown"));
}

TEST_CASE("set_value_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnSetValue(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=setvalue"));
}

TEST_CASE("set_value_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnSetValue(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"home/phase6/lamp/set", yaha::Value{std::string{"on"}}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=setvalue"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("run_publishes_startup_markers_and_requests_controller_sync", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setKnownNodeIds({kNodeIdNine, kNodeIdSeven});
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.run();

    CHECK(controller->requestConfigCalls() == 1U);
    REQUIRE(published.size() == 4U);
    CHECK(published[0].topic() == "system/zwave/removefailednode");
    CHECK(published[1].topic() == "system/zwave/addnode");
    CHECK(published[2].topic() == "system/zwave/scan");
    CHECK(published[3].topic() == "$MONITOR/zwave/nodes/known");

    REQUIRE(std::holds_alternative<double>(published[0].value()));
    REQUIRE(std::holds_alternative<double>(published[0].value()));
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<double>(published[0].value()) == 0.0);
    CHECK(std::get<double>(published[0].value()) == 0.0);
    CHECK(std::get<std::string>(published[2].value()) == "off");
    REQUIRE(std::holds_alternative<std::string>(published[3].value()));
    CHECK(std::get<std::string>(published[3].value()) == "{\"nodes\":[9,7]}");
    CHECK(hasReasonMessage(published[0], "zwave service restarted"));
    CHECK(hasReasonMessage(published[1], "zwave service restarted"));
    CHECK(hasReasonMessage(published[2], "zwave service restarted"));
    CHECK(hasReasonMessage(published[3], "zwave known nodes snapshot on service startup"));
}

TEST_CASE("run_request_config_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnRequestConfig(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.run();

    REQUIRE(published.size() == 5U);
    CHECK(published.back().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.back(), "operation=requestconfig"));
}

TEST_CASE("run_request_config_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnRequestConfig(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.run();

    REQUIRE(published.size() == 5U);
    CHECK(published.back().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.back(), "operation=requestconfig"));
    CHECK(hasReasonMessage(published.back(), "unknown"));
}

TEST_CASE("close_delegates_to_controller", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    service.close();

    CHECK(controller->closeCalls() == 1U);
}

TEST_CASE("close_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnClose(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.close();

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=close"));
}

TEST_CASE("close_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnClose(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.close();

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "system/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=close"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
}
