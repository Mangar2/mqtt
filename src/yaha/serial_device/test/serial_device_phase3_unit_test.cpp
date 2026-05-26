#include <catch2/catch_test_macros.hpp>

#include "yaha/serial_device/serial_device_serial_to_mqtt_mapper.h"

namespace {

constexpr std::uint16_t k_light_on_time_seconds{3600U};

[[nodiscard]] yaha::SerialDeviceConfig makePhase3MapperConfig() {
    yaha::SerialDeviceConfig config{};

    yaha::SerialDeviceInterfaceDefinition serialDefinition{};
    serialDefinition.commandMap["l"] = "Light/light on time";
    serialDefinition.receiverMap["level0/room1/device1/"] = "5";
    serialDefinition.receiverMapProvided = true;
    serialDefinition.valueMap["LightOnOff"] = yaha::SerialDeviceValueMapDefinition{
        .description = "light on/off map",
        .usedBy = {"l"},
        .map = {{"on", k_light_on_time_seconds}, {"off", 0U}}};

    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["12322324/2111"] = "level0/room1/fs20/task/one";

    yaha::SerialDeviceInterfaceDefinition switchDefinition{};
    switchDefinition.topicMap["level0/room1/switch/one"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "switch",
        .value = 1U,
        .address = "main"};
    switchDefinition.topicMap["level0/room1/switch/two"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "switch",
        .value = 2U,
        .address = "main"};
    switchDefinition.topicMap["level0/room1/switch/skip"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "other",
        .value = 4U,
        .address = "main"};

    config.interfaces["serial"] = serialDefinition;
    config.interfaces["fs20"] = fs20Definition;
    config.interfaces["switch"] = switchDefinition;
    return config;
}

} // namespace

TEST_CASE("serial_to_mqtt_maps_numeric_value_via_reverse_value_map", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "serial",
        .sender = yaha::SerialDeviceEndpoint{5},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "l",
        .value = yaha::SerialDeviceValue{0},
        .action = ""};

    const std::vector<yaha::Message> outputMessages = yaha::mapSerialMessageToMqttMessages(config, inputMessage);

    REQUIRE(outputMessages.size() == 1U);
    CHECK(outputMessages.front().topic() == "level0/room1/device1/Light/light on time");
    REQUIRE(std::holds_alternative<std::string>(outputMessages.front().value()));
    CHECK(std::get<std::string>(outputMessages.front().value()) == "off");
}

TEST_CASE("serial_to_mqtt_throws_on_unknown_interface", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "unknown",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "x",
        .value = yaha::SerialDeviceValue{1},
        .action = ""};

    REQUIRE_THROWS_AS(yaha::mapSerialMessageToMqttMessages(config, inputMessage), std::runtime_error);
}

TEST_CASE("serial_to_mqtt_throws_on_unknown_sender_address", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "serial",
        .sender = yaha::SerialDeviceEndpoint{"not-a-number"},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "l",
        .value = yaha::SerialDeviceValue{1},
        .action = ""};

    REQUIRE_THROWS_AS(yaha::mapSerialMessageToMqttMessages(config, inputMessage), std::runtime_error);
}

TEST_CASE("serial_to_mqtt_throws_on_unknown_command", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "fs20",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "missing",
        .value = yaha::SerialDeviceValue{"on"},
        .action = ""};

    REQUIRE_THROWS_AS(yaha::mapSerialMessageToMqttMessages(config, inputMessage), std::runtime_error);
}

TEST_CASE("serial_to_mqtt_switch_branch_handles_non_matching_topic_entries", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "switch",
        .sender = yaha::SerialDeviceEndpoint{"main"},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "switch",
        .value = yaha::SerialDeviceValue{2},
        .action = ""};

    const std::vector<yaha::Message> outputMessages = yaha::mapSerialMessageToMqttMessages(config, inputMessage);

    REQUIRE(outputMessages.size() == 2U);
    CHECK(outputMessages.front().topic() == "level0/room1/switch/one");
    CHECK(outputMessages.back().topic() == "level0/room1/switch/two");
}

TEST_CASE("serial_to_mqtt_switch_with_monostate_sender_returns_no_matches", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makePhase3MapperConfig();

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "switch",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "switch",
        .value = yaha::SerialDeviceValue{2},
        .action = ""};

    const std::vector<yaha::Message> outputMessages = yaha::mapSerialMessageToMqttMessages(config, inputMessage);

    CHECK(outputMessages.empty());
}

TEST_CASE("serial_to_mqtt_sender_numeric_with_non_numeric_address_triggers_no_match", "[serial_device]") {
    yaha::SerialDeviceConfig config{};
    yaha::SerialDeviceInterfaceDefinition serialDefinition{};
    serialDefinition.commandMap["x"] = "topic/x";
    serialDefinition.receiverMap["prefix/"] = "not-number";
    serialDefinition.receiverMapProvided = true;
    config.interfaces["serial"] = serialDefinition;

    const yaha::SerialDeviceMessage inputMessage{
        .interfaceName = "serial",
        .sender = yaha::SerialDeviceEndpoint{5},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "x",
        .value = yaha::SerialDeviceValue{1},
        .action = ""};

    REQUIRE_THROWS_AS(yaha::mapSerialMessageToMqttMessages(config, inputMessage), std::runtime_error);
}
