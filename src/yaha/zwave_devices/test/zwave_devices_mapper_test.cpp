#include <catch2/catch_test_macros.hpp>

#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kNodeIdEight = 8U;
constexpr std::uint16_t kSwitchBinaryClass = 0x25U;
constexpr double kConfigNumericValue = 42.0;

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

TEST_CASE("value_to_topic_prefers_more_specific_mapping", "[zwave_devices]") {
    const std::vector<yaha::ZwaveDeviceConfig> devices{
        makeDevice("home/lamp/generic", 5U, std::nullopt, std::nullopt, std::nullopt, std::string{"bool"}, std::nullopt),
        makeDevice("home/lamp/specific", 5U, 0x25U, 1U, 0U, std::string{"switch"}, std::nullopt)};

    yaha::ZwaveDevicesMapper mapper{devices};

    const yaha::ZwaveValueDescriptor descriptor{
        .nodeId = 5U,
        .classId = 0x25U,
        .instance = 1U,
        .index = 0U,
        .label = std::nullopt,
        .valueId = 77U};

    const auto mapping = mapper.valueToTopicAndType(descriptor);
    REQUIRE(mapping.has_value());
    CHECK(mapping->topic == "home/lamp/specific");
    CHECK(mapping->type == "switch");
}

TEST_CASE("value_to_topic_appends_label_when_class_is_unspecified", "[zwave_devices]") {
    const std::vector<yaha::ZwaveDeviceConfig> devices{
        makeDevice("home/lamp", 7U, std::nullopt, std::nullopt, std::nullopt, std::string{"bool"}, std::nullopt)};

    yaha::ZwaveDevicesMapper mapper{devices};

    const yaha::ZwaveValueDescriptor descriptor{
        .nodeId = 7U,
        .classId = 0x25U,
        .instance = 1U,
        .index = 0U,
        .label = std::string{"switch"},
        .valueId = std::nullopt};

    const auto mapping = mapper.valueToTopicAndType(descriptor);
    REQUIRE(mapping.has_value());
    CHECK(mapping->topic == "home/lamp/switch");
}

TEST_CASE("topic_to_id_uses_label_lookup_when_class_id_missing", "[zwave_devices]") {
    const std::vector<yaha::ZwaveDeviceConfig> devices{
        makeDevice("home/lamp", 8U, std::nullopt, 1U, std::nullopt, std::nullopt, std::nullopt)};

    yaha::ZwaveDevicesMapper mapper{devices};

    yaha::ZwaveNodeMap nodes{};
    nodes[kNodeIdEight] = std::vector<yaha::ZwaveNodeObject>{
        yaha::ZwaveNodeObject{.classId = kSwitchBinaryClass, .label = "power", .instance = 1U, .index = 0U, .type = "switch"}};

    const yaha::ZwaveResolvedId resolved =
        mapper.topicToZwaveId(nodes, "home/lamp", std::optional<std::string>{"power"});

    CHECK(resolved.nodeId == kNodeIdEight);
    CHECK(resolved.classId == kSwitchBinaryClass);
    CHECK(resolved.instance == 1U);
    CHECK(resolved.index == 0U);
    CHECK(resolved.type == "switch");
}

TEST_CASE("build_write_request_routes_config_class_to_set_config_param", "[zwave_devices]") {
    const yaha::ZwaveResolvedId target{
        .nodeId = 9U,
        .classId = 0x70U,
        .instance = 1U,
        .index = 11U,
        .type = "byte"};

    const yaha::ZwaveWriteRequest request = yaha::ZwaveDevicesMapper::buildWriteRequest(target, kConfigNumericValue);
    CHECK(request.kind == yaha::ZwaveWriteKind::SetConfigParam);
    REQUIRE(std::holds_alternative<double>(request.value));
    CHECK(std::get<double>(request.value) == kConfigNumericValue);
}

TEST_CASE("build_write_request_converts_switch_on_to_boolean_true", "[zwave_devices]") {
    const yaha::ZwaveResolvedId target{
        .nodeId = 10U,
        .classId = 0x25U,
        .instance = 1U,
        .index = 0U,
        .type = "switch"};

    const yaha::ZwaveWriteRequest request =
        yaha::ZwaveDevicesMapper::buildWriteRequest(target, yaha::Value{std::string{"on"}});

    CHECK(request.kind == yaha::ZwaveWriteKind::SetValue);
    REQUIRE(std::holds_alternative<bool>(request.value));
    CHECK(std::get<bool>(request.value));
}
