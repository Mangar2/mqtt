#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "yaha/rs485_interface/rs485_topic_mapper.h"

#include <string>
#include <vector>

namespace {

constexpr std::uint8_t k_device_address{5U};
constexpr std::uint8_t k_explicit_topic_address{7U};
constexpr double k_fallback_unknown_command_value{4.0};
constexpr std::uint16_t k_explicit_bit_value{0x0004U};
constexpr std::uint16_t k_explicit_on_value{0x4004U};
constexpr std::uint16_t k_explicit_off_value{0x2004U};

[[nodiscard]] yaha::Rs485InterfaceConfig makeBaseConfig() {
    yaha::Rs485InterfaceConfig config{};

    config.interfaces["switch"] = yaha::Rs485InterfaceDefinition{
        .usedBy = {'P'},
        .map = {{"on", 1U}, {"off", 0U}}};

    config.settings['P'] = "/power";
    config.status['P'] = "/power";
    config.addresses["house/room/device"] = k_device_address;
    config.topics["house/light"] = yaha::Rs485TopicMapping{
        .command = 'L',
        .value = k_explicit_bit_value,
        .address = k_explicit_topic_address};

    return config;
}

} // namespace

TEST_CASE("rs485_topic_mapper_to_serial_uses_explicit_topic_bit_mapping", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message onMessage{"house/light", std::string{"on"}};
    const yaha::Rs485MappedSerialData onData = mapper.toSerialData(onMessage);
    REQUIRE(onData.address == k_explicit_topic_address);
    REQUIRE(onData.command == 'L');
    REQUIRE(onData.value == k_explicit_on_value);

    const yaha::Message offMessage{"house/light", std::string{"off"}};
    const yaha::Rs485MappedSerialData offData = mapper.toSerialData(offMessage);
    REQUIRE(offData.value == k_explicit_off_value);
}

TEST_CASE("rs485_topic_mapper_to_serial_uses_address_command_and_interface_mapping", "[rs485_interface]") {
    const yaha::Rs485TopicMapper mapper{makeBaseConfig()};

    const yaha::Message message{"house/room/device/power", std::string{"on"}};
    const yaha::Rs485MappedSerialData data = mapper.toSerialData(message);

    REQUIRE(data.address == k_device_address);
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
    serial.sender = k_explicit_topic_address;
    serial.command = 'L';
    serial.value = static_cast<double>(k_explicit_on_value);

    const auto onMessages = mapper.toMqttMessages(serial);
    REQUIRE(onMessages.size() == 1U);
    REQUIRE(onMessages[0].topic() == "house/light");
    REQUIRE(std::get<std::string>(onMessages[0].value()) == "on");

    serial.value = static_cast<double>(k_explicit_off_value);
    const auto offMessages = mapper.toMqttMessages(serial);
    REQUIRE(offMessages.size() == 1U);
    REQUIRE(std::get<std::string>(offMessages[0].value()) == "off");
}

TEST_CASE("rs485_topic_mapper_to_mqtt_falls_back_to_address_and_status_mapping", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeBaseConfig();
    config.topics.clear();

    const yaha::Rs485TopicMapper mapper{config};

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
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
    serial.sender = k_device_address;
    serial.command = 'Z';
    serial.value = k_fallback_unknown_command_value;

    REQUIRE_THROWS_WITH(
        mapper.toMqttMessages(serial),
        Catch::Matchers::ContainsSubstring("Unknown serial command"));
}
