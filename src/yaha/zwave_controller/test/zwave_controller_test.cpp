#include <catch2/catch_test_macros.hpp>

#include "yaha/zwave_controller/zwave_controller.h"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

constexpr std::uint16_t kSwitchBinaryClass = 0x25U;
constexpr std::uint16_t kConfigClass = 0x70U;
constexpr std::uint16_t kNodeIdEleven = 11U;
constexpr std::uint16_t kNodeIdTwelve = 12U;
constexpr std::uint16_t kNodeIdThirteen = 13U;
constexpr std::uint16_t kNodeIdFourteen = 14U;
constexpr std::uint16_t kNodeIdTwenty = 20U;
constexpr std::uint16_t kNodeIdTwentyOne = 21U;
constexpr std::uint16_t kNodeIdTwentyTwo = 22U;
constexpr std::uint8_t kIndexZero = 0U;
constexpr std::uint8_t kIndexOne = 1U;
constexpr std::uint8_t kInstanceOne = 1U;
constexpr std::uint8_t kConfigParamSeven = 7U;
constexpr std::uint8_t kConfigParamThree = 3U;
constexpr std::uint16_t kSensorMultilevelClass = 0x31U;
constexpr std::uint16_t kWakeUpClass = 0x84U;
constexpr double kConfigValueFifteen = 15.0;
constexpr double kRemoveFailedNodeValue = 13.0;
constexpr double kFractionalNodeIdValue = 13.25;
constexpr double kTemperatureValue = 22.0;
constexpr std::uint64_t kValueIdSample = 1001U;
constexpr std::int32_t kControllerResultCode = 7;
constexpr std::uint32_t kDriverReadyHomeId = 0xABCDU;
constexpr std::uint16_t kUnknownNodeId = 99U;
constexpr std::uint8_t kUnknownNotificationCodeRaw = 99U;
constexpr std::uint32_t kCommandReactionFastPollMs = 10U;
constexpr std::uint32_t kCommandReactionShortTimeoutMs = 30U;
constexpr std::uint32_t kCommandReactionPollMs = 20U;
constexpr std::uint32_t kCommandReactionDefaultTimeoutMs = 30000U;
constexpr std::uint32_t kPendingTimeoutWaitMs = 150U;
constexpr std::uint32_t kPendingPollWaitMs = 180U;

struct FakeDriverPort final : yaha::IZwaveDriverPort {
    std::size_t setValueCalls{0U};
    std::size_t setConfigCalls{0U};
    std::size_t addNodeCalls{0U};
    std::size_t removeFailedNodeCalls{0U};
    std::size_t startScanCalls{0U};
    std::size_t requestConfigCalls{0U};
    std::size_t enablePollCalls{0U};
    std::size_t requestNodeStateCalls{0U};
    std::size_t disconnectCalls{0U};

    yaha::ZwaveResolvedId lastSetValueTarget{};
    std::variant<bool, double, std::string> lastSetValuePayload{false};

    std::uint16_t lastConfigNodeId{0U};
    std::uint16_t lastConfigParamId{0U};
    double lastConfigValue{0.0};

    std::uint16_t lastRemoveFailedNode{0U};
    std::uint16_t lastRequestConfigNode{0U};
    std::uint16_t lastEnablePollNode{0U};
    std::uint16_t lastEnablePollClass{0U};
    std::uint16_t lastRequestedNodeState{0U};
    std::string lastDisconnectPath{};

    void setValue(const yaha::ZwaveResolvedId& target, const std::variant<bool, double, std::string>& value) override {
        setValueCalls += 1U;
        lastSetValueTarget = target;
        lastSetValuePayload = value;
    }

    void setConfigParam(const std::uint16_t nodeId, const std::uint16_t paramId, const double value) override {
        setConfigCalls += 1U;
        lastConfigNodeId = nodeId;
        lastConfigParamId = paramId;
        lastConfigValue = value;
    }

    void addNode() override {
        addNodeCalls += 1U;
    }

    void removeFailedNode(const std::uint16_t nodeId) override {
        removeFailedNodeCalls += 1U;
        lastRemoveFailedNode = nodeId;
    }

    void startScan() override {
        startScanCalls += 1U;
    }

    void requestAllConfigParams(const std::uint16_t nodeId) override {
        requestConfigCalls += 1U;
        lastRequestConfigNode = nodeId;
    }

    void enablePoll(const std::uint16_t nodeId, const std::uint16_t classId) override {
        enablePollCalls += 1U;
        lastEnablePollNode = nodeId;
        lastEnablePollClass = classId;
    }

    void requestNodeState(const std::uint16_t nodeId) override {
        requestNodeStateCalls += 1U;
        lastRequestedNodeState = nodeId;
    }

    void disconnect(const std::string& devicePath) override {
        disconnectCalls += 1U;
        lastDisconnectPath = devicePath;
    }
};

yaha::ZwaveController makeController(
    FakeDriverPort& driver,
    const std::uint32_t commandReactionPollIntervalMs = 500U,
    const std::uint32_t commandReactionTimeoutMs = 30000U) {
    yaha::ZwaveUsbConfig usb{};
    usb.device = "/dev/ttyUSB0";
    usb.topic = "controller/topic";
    return yaha::ZwaveController{usb, driver, commandReactionPollIntervalMs, commandReactionTimeoutMs};
}

yaha::ZwaveDeviceConfig makeDevice(
    const std::string& topic,
    const std::uint16_t nodeId,
    const std::optional<std::uint16_t> classId,
    const std::optional<std::uint8_t> instance,
    const std::optional<std::uint8_t> index,
    const std::optional<std::string>& type,
    const std::optional<std::string>& label) {
    yaha::ZwaveDeviceConfig device{};
    device.topic = topic;
    device.nodeId = nodeId;
    device.classId = classId;
    device.instance = instance;
    device.index = index;
    device.type = type;
    device.label = label;
    return device;
}

} // namespace

TEST_CASE("set_value_routes_switch_payload_to_driver_set_value", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    controller.setValue("ground/livingroom/lamp/power/set", yaha::Value{std::string{"on"}});

    CHECK(driver.setValueCalls == 1U);
    CHECK(driver.lastSetValueTarget.nodeId == kNodeIdEleven);
    CHECK(driver.lastSetValueTarget.classId == kSwitchBinaryClass);
    REQUIRE(std::holds_alternative<bool>(driver.lastSetValuePayload));
    CHECK(std::get<bool>(driver.lastSetValuePayload));
}

TEST_CASE("set_value_routes_configuration_class_to_set_config_param", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/sensor",
            kNodeIdTwelve,
            kConfigClass,
            kInstanceOne,
            kConfigParamSeven,
            std::string{"byte"},
            std::nullopt)});

    controller.setValue("ground/livingroom/sensor/param/set", yaha::Value{kConfigValueFifteen});

    CHECK(driver.setConfigCalls == 1U);
    CHECK(driver.lastConfigNodeId == kNodeIdTwelve);
    CHECK(driver.lastConfigParamId == kConfigParamSeven);
    CHECK(driver.lastConfigValue == kConfigValueFifteen);
}

TEST_CASE("set_value_routes_multilevel_without_explicit_type_as_bool_for_legacy_parity", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/zwave/shutter/southwest",
            kNodeIdThirteen,
            yaha::kZwaveSwitchMultilevelClass,
            kInstanceOne,
            kIndexZero,
            std::nullopt,
            std::nullopt)});

    controller.setValue("ground/livingroom/zwave/shutter/southwest/set", yaha::Value{std::string{"on"}});

    CHECK(driver.setValueCalls == 1U);
    CHECK(driver.lastSetValueTarget.nodeId == kNodeIdThirteen);
    CHECK(driver.lastSetValueTarget.classId == yaha::kZwaveSwitchMultilevelClass);
    CHECK(driver.lastSetValueTarget.type == "bool");
    REQUIRE(std::holds_alternative<bool>(driver.lastSetValuePayload));
    CHECK(std::get<bool>(driver.lastSetValuePayload));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("controller_operations_forward_to_driver_port", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/sensor",
            kNodeIdThirteen,
            kConfigClass,
            kInstanceOne,
            kConfigParamThree,
            std::string{"byte"},
            std::nullopt)});

    controller.addDevice();
    controller.removeFailedNode(yaha::Value{kRemoveFailedNodeValue});
    controller.startScan();
    controller.requestConfigParametersForAllNodes();
    controller.close();

    CHECK(driver.addNodeCalls == 1U);
    CHECK(driver.removeFailedNodeCalls == 1U);
    CHECK(driver.lastRemoveFailedNode == kNodeIdThirteen);
    CHECK(driver.startScanCalls == 1U);
    CHECK(driver.requestConfigCalls == 1U);
    CHECK(driver.lastRequestConfigNode == kNodeIdThirteen);
    CHECK(driver.disconnectCalls == 1U);
    CHECK(driver.lastDisconnectPath == "/dev/ttyUSB0");
}

TEST_CASE("on_value_changed_publishes_mapped_switch_as_on_off", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdFourteen,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdFourteen,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.empty() == false);
    CHECK(published.back().topic() == "ground/livingroom/lamp");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    CHECK(std::get<std::string>(published.back().value()) == "on");
}

TEST_CASE("on_value_refreshed_updates_cache_without_publishing", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdFourteen,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onValueRefreshed(
        kNodeIdFourteen,
        kSwitchBinaryClass,
        yaha::ZwaveControllerValueEvent{
            .nodeId = kNodeIdFourteen,
            .classId = kSwitchBinaryClass,
            .instance = kInstanceOne,
            .index = kIndexZero,
            .label = std::nullopt,
            .valueId = kValueIdSample,
            .value = yaha::Value{1.0},
            .type = "switch",
            .readOnly = false});

    CHECK(published.empty());

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdFourteen,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE_FALSE(published.empty());
    CHECK(published.back().topic() == "ground/livingroom/lamp");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    CHECK(std::get<std::string>(published.back().value()) == "on");
}

TEST_CASE("on_controller_command_publishes_monitoring_notification", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onControllerCommand(kControllerResultCode, "in-progress");

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITOR/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "in-progress");
}

TEST_CASE("set_value_rejects_topic_without_trailing_set", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    CHECK_THROWS_AS(
        controller.setValue("ground/livingroom/lamp/power", yaha::Value{std::string{"on"}}),
        std::runtime_error);
}

TEST_CASE("remove_failed_node_rejects_non_numeric_or_fractional_values", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    CHECK_THROWS_AS(controller.removeFailedNode(yaha::Value{std::string{"abc"}}), std::invalid_argument);
    CHECK_THROWS_AS(controller.removeFailedNode(yaha::Value{kFractionalNodeIdValue}), std::runtime_error);
    CHECK(driver.removeFailedNodeCalls == 0U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("driver_lifecycle_callbacks_publish_expected_monitoring_messages", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onDriverReady(kDriverReadyHomeId);
    controller.onDriverFailed();
    controller.onScanComplete();

    REQUIRE(published.size() == 3U);

    CHECK(published[0].topic() == "$MONITOR/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published[0].value()));
    CHECK(std::get<std::string>(published[0].value()) == "starting scan");
    REQUIRE(published[0].reason().size() == 1U);
    CHECK(published[0].reason().front().message.find("homeid=0xabcd") != std::string::npos);

    CHECK(published[1].topic() == "$MONITOR/zwave/error");
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[1].value()) == "driver failure");

    CHECK(published[2].topic() == "$MONITOR/zwave/notification");
    REQUIRE(std::holds_alternative<std::string>(published[2].value()));
    CHECK(std::get<std::string>(published[2].value()) == "scan complete");
}

TEST_CASE("driver_failed_callback_is_invoked", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::size_t callbackCalls = 0U;
    controller.setDriverFailedCallback([&callbackCalls] {
        ++callbackCalls;
    });

    controller.onDriverFailed();

    CHECK(callbackCalls == 1U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("notification_callback_maps_all_codes_and_uses_unknown_topic_fallback", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    const std::vector<yaha::ZwaveNotificationCode> notifications{
        yaha::ZwaveNotificationCode::MessageComplete,
        yaha::ZwaveNotificationCode::Timeout,
        yaha::ZwaveNotificationCode::Nop,
        yaha::ZwaveNotificationCode::NodeAwake,
        yaha::ZwaveNotificationCode::NodeSleep,
        yaha::ZwaveNotificationCode::NodeDead,
        yaha::ZwaveNotificationCode::NodeAlive};

    const std::vector<std::string> expectedText{
        "message completed",
        "timeout",
        "nop",
        "node awake",
        "node sleep",
        "node dead",
        "node alive"};

    for (const auto notification : notifications) {
        controller.onNotification(kNodeIdTwenty, notification);
    }
    controller.onNotification(kNodeIdTwenty, static_cast<yaha::ZwaveNotificationCode>(kUnknownNotificationCodeRaw));

    REQUIRE(published.size() == expectedText.size() + 1U);
    for (std::size_t index = 0U; index < expectedText.size(); ++index) {
        CHECK(published[index].topic() == "/$MONITOR/zwave/unknown node 20");
        REQUIRE(std::holds_alternative<std::string>(published[index].value()));
        CHECK(std::get<std::string>(published[index].value()) == expectedText[index]);
    }

    CHECK(published.back().topic() == "/$MONITOR/zwave/unknown node 20");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    CHECK(std::get<std::string>(published.back().value()) == "unknown");
}

TEST_CASE("node_ready_does_not_enable_global_polling", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.onNodeAdded(kNodeIdTwentyOne);
    controller.onValueAdded(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdTwentyOne,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::optional<std::string>{"binary"},
        .valueId = std::nullopt,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});
    controller.onValueAdded(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdTwentyOne,
        .classId = kSensorMultilevelClass,
        .instance = kInstanceOne,
        .index = kIndexOne,
        .label = std::optional<std::string>{"temperature"},
        .valueId = std::nullopt,
        .value = yaha::Value{kTemperatureValue},
        .type = "number",
        .readOnly = false});

    controller.onNodeReady(kNodeIdTwentyOne, yaha::ZwaveNodeInfo{});

    CHECK(driver.enablePollCalls == 0U);

    controller.onValueRemoved(kNodeIdTwentyOne, kSwitchBinaryClass, kIndexZero);
    controller.onNodeReady(kNodeIdTwentyOne, yaha::ZwaveNodeInfo{});
    CHECK(driver.enablePollCalls == 0U);

    controller.onValueRemoved(kNodeIdTwentyOne, kSensorMultilevelClass, kIndexOne);
    controller.onValueRemoved(kNodeIdTwentyOne, kSensorMultilevelClass, kIndexOne);
    controller.onValueRemoved(kUnknownNodeId, kSensorMultilevelClass, kIndexOne);
}

TEST_CASE("on_value_changed_for_usb_controller_publishes_to_usb_topic", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = 1U,
        .classId = kWakeUpClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::optional<std::string>{"controller"},
        .valueId = std::nullopt,
        .value = yaha::Value{std::string{"state"}},
        .type = "string",
        .readOnly = false});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "controller/topic");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "state");
}

TEST_CASE("on_value_changed_without_mapping_falls_back_to_monitoring_topic", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdTwentyTwo,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::optional<std::string>{"missing"},
        .valueId = std::nullopt,
        .value = yaha::Value{std::string{"open"}},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "$MONITOR/zwave/22");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "open");
}

TEST_CASE("matching_feedback_prepends_tracked_reasons", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    const std::vector<yaha::ReasonEntry> reasons{
        yaha::ReasonEntry{.message = "rule", .timestamp = "2026-05-19T10:00:00Z"},
        yaha::ReasonEntry{.message = "ui", .timestamp = "2026-05-19T09:59:00Z"}};

    controller.setValue("ground/livingroom/lamp/power/set", yaha::Value{std::string{"on"}}, reasons);
    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdEleven,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.size() == 1U);
    REQUIRE(published.front().reason().size() >= 3U);
    CHECK(published.front().reason()[0].message == "rule");
    CHECK(published.front().reason()[1].message == "ui");
    CHECK(published.front().reason()[2].message.find("received from zwave") != std::string::npos);
}

TEST_CASE("same_action_replaces_pending_entry", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "old", .timestamp = "2026-05-19T10:00:00Z"}});
    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "new", .timestamp = "2026-05-19T10:01:00Z"}});

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdEleven,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.size() == 1U);
    REQUIRE_FALSE(published.front().reason().empty());
    CHECK(published.front().reason().front().message == "new");
}

TEST_CASE("different_feedback_keeps_pending_command", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "await-on", .timestamp = "2026-05-19T10:00:00Z"}});

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdEleven,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{0.0},
        .type = "switch",
        .readOnly = false});

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdEleven,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.size() == 2U);
    CHECK(published[0].reason().front().message.find("received from zwave") != std::string::npos);
    CHECK(published[1].reason().front().message == "await-on");
}

TEST_CASE("pending_command_times_out_and_is_removed", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver, kCommandReactionFastPollMs, kCommandReactionShortTimeoutMs);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "will-timeout", .timestamp = "2026-05-19T10:00:00Z"}});

    std::this_thread::sleep_for(std::chrono::milliseconds{kPendingTimeoutWaitMs});

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdEleven,
        .classId = kSwitchBinaryClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueIdSample,
        .value = yaha::Value{1.0},
        .type = "switch",
        .readOnly = false});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().reason().front().message.find("received from zwave") != std::string::npos);
}

TEST_CASE("pending_command_polling_targets_only_affected_node", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver, kCommandReactionPollMs, kCommandReactionDefaultTimeoutMs);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "poll", .timestamp = "2026-05-19T10:00:00Z"}});

    std::this_thread::sleep_for(std::chrono::milliseconds{kPendingPollWaitMs});

    CHECK(driver.requestNodeStateCalls >= 1U);
    CHECK(driver.lastRequestedNodeState == kNodeIdEleven);
}

TEST_CASE("matching_value_refreshed_publishes_pending_feedback", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({
        makeDevice(
            "ground/livingroom/lamp",
            kNodeIdEleven,
            kSwitchBinaryClass,
            kInstanceOne,
            kIndexZero,
            std::string{"switch"},
            std::nullopt)});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.setValue(
        "ground/livingroom/lamp/power/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{yaha::ReasonEntry{.message = "await-refresh", .timestamp = "2026-05-19T10:00:00Z"}});

    controller.onValueRefreshed(
        kNodeIdEleven,
        kSwitchBinaryClass,
        yaha::ZwaveControllerValueEvent{
            .nodeId = kNodeIdEleven,
            .classId = kSwitchBinaryClass,
            .instance = kInstanceOne,
            .index = kIndexZero,
            .label = std::nullopt,
            .valueId = kValueIdSample,
            .value = yaha::Value{1.0},
            .type = "switch",
            .readOnly = false});

    REQUIRE(published.size() == 1U);
    CHECK(published.front().topic() == "ground/livingroom/lamp");
    REQUIRE(std::holds_alternative<std::string>(published.front().value()));
    CHECK(std::get<std::string>(published.front().value()) == "on");
    REQUIRE_FALSE(published.front().reason().empty());
    CHECK(published.front().reason().front().message == "await-refresh");
}
