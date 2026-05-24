#include <catch2/catch_test_macros.hpp>

#include "yaha/zwave_controller/zwave_controller.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

constexpr std::uint16_t kNodeIdSeven = 7U;
constexpr std::uint16_t kNodeIdNine = 9U;
constexpr std::uint16_t kNodeIdEleven = 11U;
constexpr std::uint32_t kFullDevicePollMs = 600000U;
constexpr std::uint32_t kCommandReactionPollMs = 20U;
constexpr std::uint32_t kCommandReactionTimeoutMs = 30000U;

struct FakeDriverPort final : yaha::IZwaveDriverPort {
    void setValue(const yaha::ZwaveResolvedId& target,
                  const std::variant<bool, double, std::string>& value) override {
        (void)target;
        (void)value;
    }
    void setConfigParam(std::uint16_t nodeId, std::uint16_t paramId, double value) override {
        (void)nodeId;
        (void)paramId;
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

[[nodiscard]] yaha::ZwaveDeviceConfig makeDevice(
    const std::string& topic,
    const std::uint16_t nodeId,
    const std::optional<std::uint16_t> classId = std::nullopt) {
    yaha::ZwaveDeviceConfig device{};
    device.topic = topic;
    device.nodeId = nodeId;
    device.classId = classId;
    return device;
}

} // namespace

TEST_CASE("known_node_ids_include_configured_and_runtime_nodes_sorted_unique", "[zwave_controller]") {
    FakeDriverPort driver{};
    yaha::ZwaveUsbConfig usb{};
    usb.device = "/dev/ttyUSB0";
    usb.topic = "controller/topic";

    yaha::ZwaveController controller{
        usb,
        driver,
        kFullDevicePollMs,
        kCommandReactionPollMs,
        kCommandReactionTimeoutMs};
    controller.setDeviceConfiguration({
        makeDevice("home/node9/switch", kNodeIdNine, yaha::kZwaveSwitchBinaryClass),
        makeDevice("home/node7/sensor", kNodeIdSeven),
        makeDevice("home/node9/status", kNodeIdNine)});

    controller.onNodeAdded(kNodeIdEleven);

    const std::vector<std::uint16_t> knownNodeIds = controller.knownNodeIds();
    CHECK(knownNodeIds == std::vector<std::uint16_t>{kNodeIdSeven, kNodeIdNine, kNodeIdEleven});
}

TEST_CASE("known_node_ids_drop_removed_runtime_nodes_but_keep_configured_nodes", "[zwave_controller]") {
    FakeDriverPort driver{};
    yaha::ZwaveUsbConfig usb{};
    usb.device = "/dev/ttyUSB0";
    usb.topic = "controller/topic";

    yaha::ZwaveController controller{
        usb,
        driver,
        kFullDevicePollMs,
        kCommandReactionPollMs,
        kCommandReactionTimeoutMs};
    controller.setDeviceConfiguration({
        makeDevice("home/node7/sensor", kNodeIdSeven)});

    controller.onNodeAdded(kNodeIdNine);
    controller.onNodeAdded(kNodeIdEleven);
    controller.onNodeRemoved(kNodeIdEleven);

    const std::vector<std::uint16_t> knownNodeIds = controller.knownNodeIds();
    CHECK(knownNodeIds == std::vector<std::uint16_t>{kNodeIdSeven, kNodeIdNine});
}
