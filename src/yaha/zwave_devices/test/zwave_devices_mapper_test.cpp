#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kNodeIdEight = 8U;
constexpr std::uint16_t kNodeIdNine = 9U;
constexpr std::uint16_t kSwitchBinaryClass = 0x25U;
constexpr double kConfigNumericValue = 42.0;
constexpr double kByteNumericInput = 12.75;
constexpr double kDoubleTolerance = 1e-9;

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

TEST_CASE("value_to_topic_uses_cached_value_id_mapping", "[zwave_devices]") {
    const std::vector<yaha::ZwaveDeviceConfig> devices{
        makeDevice("home/old", 12U, 0x25U, 1U, 0U, std::string{"switch"}, std::nullopt),
        makeDevice("home/new", 12U, 0x25U, 2U, 1U, std::string{"byte"}, std::nullopt)};

    yaha::ZwaveDevicesMapper mapper{devices};

    const yaha::ZwaveValueDescriptor first{
        .nodeId = 12U,
        .classId = 0x25U,
        .instance = 1U,
        .index = 0U,
        .label = std::nullopt,
        .valueId = 555U};

    const yaha::ZwaveValueDescriptor second{
        .nodeId = 12U,
        .classId = 0x25U,
        .instance = 2U,
        .index = 1U,
        .label = std::nullopt,
        .valueId = 555U};

    const auto firstMapping = mapper.valueToTopicAndType(first);
    const auto secondMapping = mapper.valueToTopicAndType(second);

    REQUIRE(firstMapping.has_value());
    REQUIRE(secondMapping.has_value());
    CHECK(firstMapping->topic == "home/old");
    CHECK(secondMapping->topic == "home/old");
}

TEST_CASE("topic_to_id_throws_on_unknown_topic", "[zwave_devices]") {
    yaha::ZwaveDevicesMapper mapper{{
        makeDevice("home/known", 2U, kSwitchBinaryClass, 1U, 0U, std::string{"switch"}, std::nullopt),
    }};

    yaha::ZwaveNodeMap nodes{};
    REQUIRE_THROWS_WITH(
        mapper.topicToZwaveId(nodes, "home/unknown", std::nullopt),
        Catch::Matchers::ContainsSubstring("unknown topic"));
}

TEST_CASE("topic_to_id_label_lookup_reports_missing_node_and_label", "[zwave_devices]") {
    yaha::ZwaveDevicesMapper mapper{{
        makeDevice("home/lamp", kNodeIdNine, std::nullopt, 1U, std::nullopt, std::nullopt, std::nullopt),
    }};

    yaha::ZwaveNodeMap emptyNodes{};
    REQUIRE_THROWS_WITH(
        mapper.topicToZwaveId(emptyNodes, "home/lamp", std::optional<std::string>{"power"}),
        Catch::Matchers::ContainsSubstring("node not found"));

    yaha::ZwaveNodeMap nodesWithDifferentLabel{};
    nodesWithDifferentLabel[kNodeIdNine] = std::vector<yaha::ZwaveNodeObject>{
        yaha::ZwaveNodeObject{.classId = kSwitchBinaryClass, .label = "other", .instance = 1U, .index = 0U, .type = "switch"}};

    REQUIRE_THROWS_WITH(
        mapper.topicToZwaveId(nodesWithDifferentLabel, "home/lamp", std::optional<std::string>{"power"}),
        Catch::Matchers::ContainsSubstring("label not found"));
}

TEST_CASE("build_write_request_covers_byte_numeric_and_text_passthrough", "[zwave_devices]") {
    const yaha::ZwaveResolvedId byteTarget{
        .nodeId = 20U,
        .classId = 0x31U,
        .instance = 1U,
        .index = 1U,
        .type = "byte"};

    const yaha::ZwaveWriteRequest byteRequest =
        yaha::ZwaveDevicesMapper::buildWriteRequest(byteTarget, yaha::Value{std::string{"12.75"}});
    CHECK(byteRequest.kind == yaha::ZwaveWriteKind::SetValue);
    REQUIRE(std::holds_alternative<double>(byteRequest.value));
    CHECK(std::abs(std::get<double>(byteRequest.value) - kByteNumericInput) < kDoubleTolerance);

    const yaha::ZwaveResolvedId textTarget{
        .nodeId = 21U,
        .classId = 0x30U,
        .instance = 1U,
        .index = 1U,
        .type = "text"};

    const yaha::ZwaveWriteRequest textRequest =
        yaha::ZwaveDevicesMapper::buildWriteRequest(textTarget, yaha::Value{std::string{"hello"}});
    CHECK(textRequest.kind == yaha::ZwaveWriteKind::SetValue);
    REQUIRE(std::holds_alternative<std::string>(textRequest.value));
    CHECK(std::get<std::string>(textRequest.value) == "hello");
}

TEST_CASE("build_write_request_byte_rejects_invalid_numeric_text", "[zwave_devices]") {
    const yaha::ZwaveResolvedId byteTarget{
        .nodeId = 22U,
        .classId = 0x31U,
        .instance = 1U,
        .index = 1U,
        .type = "byte"};

    REQUIRE_THROWS_WITH(
        yaha::ZwaveDevicesMapper::buildWriteRequest(byteTarget, yaha::Value{std::string{"12x"}}),
        Catch::Matchers::ContainsSubstring("invalid numeric value"));
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
