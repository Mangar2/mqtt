#include <catch2/catch_test_macros.hpp>

#include "yaha/serial_device/serial_device_message.h"
#include "yaha/serial_device/serial_device_parser.h"
#include "yaha/serial_device/serial_device_wire_serializer.h"

namespace {

[[nodiscard]] yaha::SerialDeviceMessage makeSwitchMessage(const std::int64_t value) {
    return yaha::SerialDeviceMessage{
        .interfaceName = "switch",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{"main"},
        .command = "switch",
        .value = yaha::SerialDeviceValue{value},
        .action = ""};
}

} // namespace

TEST_CASE("serial_device_message_to_string_formats_legacy_debug_layout", "[serial_device]") {
    const yaha::SerialDeviceMessage message{
        .interfaceName = "i2c",
        .sender = yaha::SerialDeviceEndpoint{3},
        .receiver = yaha::SerialDeviceEndpoint{"kitchen"},
        .command = "L",
        .value = yaha::SerialDeviceValue{42},
        .action = ""};

    const std::string debugText = message.toString();

    CHECK(debugText == "Interface: i2c Sender: 3 Receiver: kitchen Command: L Value: 42");
}

TEST_CASE("serial_device_parser_ignores_unknown_objects", "[serial_device]") {
    yaha::SerialDeviceStreamParser parser{};

    const auto noMessageFromUnknownObject = parser.parseChunk("noise{\"X\":1}");
    REQUIRE_FALSE(noMessageFromUnknownObject.has_value());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_parser_reassembles_partial_serial_object", "[serial_device]") {
    yaha::SerialDeviceStreamParser parser{};

    const auto noMessageFromPartial = parser.parseChunk("{\"S\":1");
    REQUIRE_FALSE(noMessageFromPartial.has_value());

    const auto parsedSerialMessage = parser.parseChunk(R"(,"R":2,"K":"t","V":19})");
    REQUIRE(parsedSerialMessage.has_value());
    CHECK(parsedSerialMessage->interfaceName == "serial");
    CHECK(parsedSerialMessage->sender == yaha::SerialDeviceEndpoint{1});
    CHECK(parsedSerialMessage->receiver == yaha::SerialDeviceEndpoint{2});
    CHECK(parsedSerialMessage->command == "t");
    CHECK(parsedSerialMessage->value == yaha::SerialDeviceValue{19});
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_parser_converts_switch_bit_string_value", "[serial_device]") {
    yaha::SerialDeviceStreamParser parser{};

    const auto parsedMessage = parser.parseChunk(R"([5,"switch","0110"])");

    REQUIRE(parsedMessage.has_value());
    CHECK(parsedMessage->interfaceName == "switch");
    CHECK(parsedMessage->sender == yaha::SerialDeviceEndpoint{5});
    CHECK(parsedMessage->receiver == yaha::SerialDeviceEndpoint{});
    CHECK(parsedMessage->command == "switch");
    CHECK(parsedMessage->value == yaha::SerialDeviceValue{6});
    CHECK(parsedMessage->action.empty());
}

TEST_CASE("serial_device_wire_serializer_switch_routes_cover_supported_and_invalid_values", "[serial_device]") {
    CHECK(yaha::serialDeviceMessageToWireString(makeSwitchMessage(16386)) == "s1H");
    CHECK(yaha::serialDeviceMessageToWireString(makeSwitchMessage(8196)) == "s2L");
    CHECK(yaha::serialDeviceMessageToWireString(makeSwitchMessage(2)) == "s1");
    CHECK(yaha::serialDeviceMessageToWireString(makeSwitchMessage(16896)) == "");
}

TEST_CASE("serial_device_wire_serializer_serial_json_uses_legacy_field_order_and_escaping", "[serial_device]") {
    const yaha::SerialDeviceMessage serialMessage{
        .interfaceName = "serial",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{"R\\1"},
        .command = "x\"y",
        .value = yaha::SerialDeviceValue{"v\"q"},
        .action = ""};

    const std::string payload = yaha::serialDeviceMessageToWireString(serialMessage);

    CHECK(payload == "{\"S\":null,\"R\":\"R\\\\1\",\"C\":\"x\\\"y\",\"V\":\"v\\\"q\"}");
}

TEST_CASE("serial_device_wire_serializer_handles_unknown_interface_and_non_numeric_switch_payload", "[serial_device]") {
    const yaha::SerialDeviceMessage unknownInterface{
        .interfaceName = "other",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "c",
        .value = yaha::SerialDeviceValue{1},
        .action = ""};
    CHECK(yaha::serialDeviceMessageToWireString(unknownInterface).empty());

    const yaha::SerialDeviceMessage nonNumericSwitch{
        .interfaceName = "switch",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "switch",
        .value = yaha::SerialDeviceValue{"abc"},
        .action = ""};
    CHECK(yaha::serialDeviceMessageToWireString(nonNumericSwitch).empty());

    const yaha::SerialDeviceMessage fs20NoSlash{
        .interfaceName = "fs20",
        .sender = yaha::SerialDeviceEndpoint{},
        .receiver = yaha::SerialDeviceEndpoint{},
        .command = "noslash",
        .value = yaha::SerialDeviceValue{"on"},
        .action = ""};
    CHECK(yaha::serialDeviceMessageToWireString(fs20NoSlash) == "Gon");
}
