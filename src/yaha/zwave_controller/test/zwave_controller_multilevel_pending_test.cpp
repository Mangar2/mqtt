#include <catch2/catch_test_macros.hpp>

#include "yaha/zwave_controller/zwave_controller.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

constexpr std::uint16_t kNodeIdThirteen = 13U;
constexpr std::uint16_t kSwitchMultilevelClass = 0x26U;
constexpr std::uint8_t kInstanceOne = 1U;
constexpr std::uint8_t kIndexZero = 0U;
constexpr std::uint64_t kValueId = 222920721U;
constexpr double kRollerOpenPercent = 99.0;
constexpr std::uint32_t kCommandReactionPollMs = 20U;
constexpr std::uint32_t kCommandReactionTimeoutMs = 30000U;
constexpr std::uint32_t kFullPollDisabledMs = 600000U;

struct FakeDriverPort final : yaha::IZwaveDriverPort {
    void setValue(const yaha::ZwaveResolvedId& target, const std::variant<bool, double, std::string>& value) override {
        (void)target;
        (void)value;
    }
    void setConfigParam(std::uint16_t nodeId, std::uint16_t parameterId, double value) override {
        (void)nodeId;
        (void)parameterId;
        (void)value;
    }
    void addNode() override {}
    void removeFailedNode(std::uint16_t nodeId) override {
        (void)nodeId;
    }
    void startScan() override {}
    void requestAllConfigParams(std::uint16_t nodeId) override {
        (void)nodeId;
    }
    void enablePoll(std::uint16_t nodeId, std::uint16_t classId) override {
        (void)nodeId;
        (void)classId;
    }
    void requestNodeState(std::uint16_t nodeId) override {
        (void)nodeId;
    }
    void requestNodeInfo(std::uint16_t nodeId) override {
        (void)nodeId;
    }
    void disconnect(const std::string& devicePath) override {
        (void)devicePath;
    }
};

yaha::ZwaveController makeController(FakeDriverPort& driver) {
    yaha::ZwaveUsbConfig usb{};
    usb.device = "/dev/ttyUSB0";
    usb.topic = "controller/topic";
    return yaha::ZwaveController{
        usb,
        driver,
        kFullPollDisabledMs,
        kCommandReactionPollMs,
        kCommandReactionTimeoutMs};
}

yaha::ZwaveDeviceConfig makeShutterDevice() {
    yaha::ZwaveDeviceConfig device{};
    device.topic = "ground/livingroom/zwave/shutter/southwest";
    device.nodeId = kNodeIdThirteen;
    device.classId = kSwitchMultilevelClass;
    device.instance = kInstanceOne;
    device.index = kIndexZero;
    return device;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("pending_match_accepts_multilevel_nonzero_feedback_for_on", "[zwave_controller]") {
    FakeDriverPort driver{};
    auto controller = makeController(driver);

    controller.setDeviceConfiguration({makeShutterDevice()});

    std::vector<yaha::Message> published{};
    controller.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    controller.setValue(
        "ground/livingroom/zwave/shutter/southwest/set",
        yaha::Value{std::string{"on"}},
        std::vector<yaha::ReasonEntry>{
            yaha::ReasonEntry{.message = "Rule: UpMorning", .timestamp = "2026-05-24T05:30:10Z"},
            yaha::ReasonEntry{.message = "received by zwave service", .timestamp = "2026-05-24T05:30:10Z"}});

    controller.onValueChanged(yaha::ZwaveControllerValueEvent{
        .nodeId = kNodeIdThirteen,
        .classId = kSwitchMultilevelClass,
        .instance = kInstanceOne,
        .index = kIndexZero,
        .label = std::nullopt,
        .valueId = kValueId,
        .value = yaha::Value{kRollerOpenPercent},
        .type = "number",
        .readOnly = false});

    REQUIRE(published.size() == 2U);
    CHECK(published[1].topic() == "ground/livingroom/zwave/shutter/southwest");
    REQUIRE(std::holds_alternative<double>(published[1].value()));
    CHECK(std::get<double>(published[1].value()) == kRollerOpenPercent);
    REQUIRE(published[1].reason().size() >= 3U);
    CHECK(published[1].reason()[0].message == "Rule: UpMorning");
    CHECK(published[1].reason()[1].message == "received by zwave service");
}
