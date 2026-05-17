#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_interface/rs485_topic_mapper.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] yaha::Rs485InterfaceConfig makeBaseConfig() {
    yaha::Rs485InterfaceConfig config{};

    config.interfaces["switch"] = yaha::Rs485InterfaceDefinition{
        .usedBy = {'P'},
        .map = {{"on", 1U}, {"off", 0U}}};

    config.settings['P'] = "/power";
    config.status['P'] = "/power";
    config.addresses["house/room/device"] = 5U;
    config.topics["house/light"] = yaha::Rs485TopicMapping{
        .command = 'L',
        .value = 0x0004U,
        .address = 7U};

    return config;
}

} // namespace

TEST_CASE("rs485_topic_mapper_to_serial_uses_explicit_topic_bit_mapping", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message onMessage{"house/light", std::string{"on"}};
    const yaha::Rs485MappedSerialData onData = mapper.toSerialData(onMessage);
    REQUIRE(onData.address == 7U);
    REQUIRE(onData.command == 'L');
    REQUIRE(onData.value == static_cast<std::uint16_t>(0x4004U));

    const yaha::Message offMessage{"house/light", std::string{"off"}};
    const yaha::Rs485MappedSerialData offData = mapper.toSerialData(offMessage);
    REQUIRE(offData.value == static_cast<std::uint16_t>(0x2004U));
}

TEST_CASE("rs485_topic_mapper_to_serial_uses_address_command_and_interface_mapping", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message message{"house/room/device/power", std::string{"on"}};
    const yaha::Rs485MappedSerialData data = mapper.toSerialData(message);

    REQUIRE(data.address == 5U);
    REQUIRE(data.command == 'P');
    REQUIRE(data.value == 1U);
}

TEST_CASE("rs485_topic_mapper_to_serial_rejects_unknown_topic_prefix", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message message{"other/location/device/power", std::string{"on"}};
    REQUIRE_THROWS_WITH(
        mapper.toSerialData(message),
        Catch::Matchers::ContainsSubstring("undefined device address"));
}

TEST_CASE("rs485_topic_mapper_to_serial_rejects_unknown_value_mapping", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message message{"house/room/device/power", std::string{"invalid"}};
    REQUIRE_THROWS_WITH(
        mapper.toSerialData(message),
        Catch::Matchers::ContainsSubstring("not an integer"));
}

TEST_CASE("rs485_topic_mapper_to_mqtt_uses_explicit_topics_and_switch_bits", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    yaha::Rs485SerialMessage serial{};
    serial.sender = 7U;
    serial.command = 'L';
    serial.value = static_cast<double>(0x4004U);

    const auto onMessages = mapper.toMqttMessages(serial);
    REQUIRE(onMessages.size() == 1U);
    REQUIRE(onMessages[0].topic() == "house/light");
    REQUIRE(std::get<std::string>(onMessages[0].value()) == "on");

    serial.value = static_cast<double>(0x2004U);
    const auto offMessages = mapper.toMqttMessages(serial);
    REQUIRE(offMessages.size() == 1U);
    REQUIRE(std::get<std::string>(offMessages[0].value()) == "off");
}

TEST_CASE("rs485_topic_mapper_to_mqtt_falls_back_to_address_and_status_mapping", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeBaseConfig();
    config.topics.clear();

    const yaha::Rs485TopicMapper mapper{config};

    yaha::Rs485SerialMessage serial{};
    serial.sender = 5U;
    serial.command = 'P';
    serial.value = 1.0;

    const auto messages = mapper.toMqttMessages(serial);
    REQUIRE(messages.size() == 1U);
    REQUIRE(messages[0].topic() == "house/room/device/power");
    REQUIRE(std::holds_alternative<std::string>(messages[0].value()));
    REQUIRE(std::get<std::string>(messages[0].value()) == "on");
}

TEST_CASE("rs485_topic_mapper_to_mqtt_rejects_unknown_command", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeBaseConfig();
    config.topics.clear();

    const yaha::Rs485TopicMapper mapper{config};

    yaha::Rs485SerialMessage serial{};
    serial.sender = 5U;
    serial.command = 'Z';
    serial.value = 4.0;

    REQUIRE_THROWS_WITH(
        mapper.toMqttMessages(serial),
        Catch::Matchers::ContainsSubstring("Unknown serial command"));
}
