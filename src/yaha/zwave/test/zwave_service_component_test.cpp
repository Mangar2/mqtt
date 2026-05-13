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

    void setValue(const std::string& topic, const yaha::Value& value) override {
        if (throwUnknownOnSetValue_) {
            throw kUnknownThrowSetValue;
        }
        if (throwOnSetValue_) {
            throw std::runtime_error{"setvalue failed in fake controller"};
        }
        setValueCalls_ += 1U;
        lastSetTopic_ = topic;
        lastSetValue_ = value;
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

    [[nodiscard]] std::size_t setValueCalls() const {
        return setValueCalls_;
    }

    [[nodiscard]] const std::string& lastSetTopic() const {
        return lastSetTopic_;
    }

    [[nodiscard]] const yaha::Value& lastSetValue() const {
        return lastSetValue_;
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

    REQUIRE(subscriptions.contains("$MONITORING/zwave/removefailednode/set"));
    REQUIRE(subscriptions.contains("$MONITORING/zwave/addnode/set"));
    REQUIRE(subscriptions.contains("$MONITORING/zwave/scan/set"));
    CHECK(subscriptions.at("$MONITORING/zwave/removefailednode/set") == yaha::Qos::ExactlyOnce);
    CHECK(subscriptions.at("$MONITORING/zwave/addnode/set") == yaha::Qos::ExactlyOnce);
    CHECK(subscriptions.at("$MONITORING/zwave/scan/set") == yaha::Qos::ExactlyOnce);

    REQUIRE(subscriptions.contains("home/lamp/set"));
    REQUIRE(subscriptions.contains("home/climate/+/set"));
    CHECK(subscriptions.at("home/lamp/set") == yaha::Qos::AtMostOnce);
    CHECK(subscriptions.at("home/climate/+/set") == yaha::Qos::AtMostOnce);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("management_messages_are_forwarded_and_scan_success_is_published", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});
    service.handleMessage(yaha::Message{"$MONITORING/zwave/addnode/set", yaha::Value{std::string{"ignored"}}});
    service.handleMessage(yaha::Message{"$MONITORING/zwave/scan/set", yaha::Value{std::string{"now"}}});

    CHECK(controller->removeFailedCalls() == 1U);
    CHECK(controller->addDeviceCalls() == 1U);
    CHECK(controller->startScanCalls() == 1U);
    REQUIRE(std::holds_alternative<double>(controller->lastRemoveFailedValue()));
    CHECK(std::get<double>(controller->lastRemoveFailedValue()) == kRemoveFailedPayload);

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "scan command accepted");
    CHECK(published.front().qos() == yaha::Qos::ExactlyOnce);
    CHECK(published.front().retain());
}

TEST_CASE("scan_failure_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnScan(true);

    yaha::ZwaveServiceComponent service{makeConfig(), controller};
    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/scan/set", yaha::Value{std::string{"now"}}});

    REQUIRE(controller->startScanCalls() == 1U);
    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "scan command failed");
    CHECK(hasReasonMessage(published.front(), "scan failed in fake controller"));
}

TEST_CASE("scan_unknown_failure_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnScan(true);

    yaha::ZwaveServiceComponent service{makeConfig(), controller};
    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/scan/set", yaha::Value{std::string{"now"}}});

    REQUIRE(controller->startScanCalls() == 0U);
    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "scan command failed");
    CHECK(hasReasonMessage(published.front(), "unknown"));
}

TEST_CASE("publish_without_callback_logs_error", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::ostringstream outputStream{};
    auto* previousBuffer = std::cout.rdbuf(outputStream.rdbuf());
    service.run();
    std::cout.rdbuf(previousBuffer);

    REQUIRE(outputStream.str().find("zwave_service[error] op=publish reason=callback_missing")
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

    REQUIRE(outputStream.str().find("zwave_service[error] op=publish reason=publish_rejected")
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

    REQUIRE(outputStream.str().find("zwave_service[error] op=publish reason=exception")
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

    REQUIRE(outputStream.str().find("zwave_service[error] op=publish reason=exception")
            != std::string::npos);
    REQUIRE(outputStream.str().find("detail=\"unknown\"") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("regular_set_message_updates_matcher_and_publish_flags", "[zwave_service]") {
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

    yaha::Message controllerPublish{"home/phase6/lamp", yaha::Value{std::string{"on"}}};
    controllerPublish.addReason("device feedback");
    controller->emitControllerPublish(controllerPublish);

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "home/phase6/lamp");
    CHECK(published.front().qos() == yaha::Qos::ExactlyOnce);
    CHECK(published.front().retain());
    CHECK(hasReasonMessage(published.front(), "received by zwave service"));
    CHECK(hasReasonMessage(published.front(), "device feedback"));
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

    service.handleMessage(yaha::Message{"$MONITORING/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=removefailednode"));
}

TEST_CASE("remove_failed_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnRemoveFailed(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/removefailednode/set", yaha::Value{kRemoveFailedPayload}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=removefailednode"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
}

TEST_CASE("add_node_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowOnAddDevice(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/addnode/set", yaha::Value{std::string{"ignored"}}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=addnode"));
}

TEST_CASE("add_node_unknown_exception_publishes_error_message", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    controller->setThrowUnknownOnAddDevice(true);
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.handleMessage(yaha::Message{"$MONITORING/zwave/addnode/set", yaha::Value{std::string{"ignored"}}});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=addnode"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
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
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
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
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=setvalue"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("run_publishes_startup_markers_and_requests_controller_sync", "[zwave_service]") {
    auto controller = std::make_shared<FakeController>();
    yaha::ZwaveServiceComponent service{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    service.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    service.run();

    CHECK(controller->requestConfigCalls() == 1U);
    REQUIRE(published.size() == 2U);
    CHECK(published[0].topic() == "$MONITORING/zwave/removefailednode");
    CHECK(published[1].topic() == "$MONITORING/zwave/addnode");

    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[0].value()) == "nop");
    CHECK(std::get<std::string>(published[1].value()) == "nop");
    CHECK(hasReasonMessage(published[0], "zwave restarted"));
    CHECK(hasReasonMessage(published[1], "zwave restarted"));
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

    REQUIRE(published.size() == 3U);
    CHECK(published.back().topic() == "$MONITORING/zwave/error");
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

    REQUIRE(published.size() == 3U);
    CHECK(published.back().topic() == "$MONITORING/zwave/error");
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
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
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
    CHECK(published.front().topic() == "$MONITORING/zwave/error");
    CHECK(hasReasonMessage(published.front(), "operation=close"));
    CHECK(hasReasonMessage(published.front(), "unknown"));
}
