#include <catch2/catch_test_macros.hpp>

#include "json/json_value.h"
#include "yaha/serial_device/serial_device_mqtt_to_serial_mapper.h"
#include "yaha/serial_device/serial_device_serial_to_mqtt_mapper.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t k_light_on_time_seconds{3600U};

[[nodiscard]] std::string readTextFile(const std::filesystem::path& pathValue) {
    std::ifstream stream{pathValue};
    REQUIRE(stream.good());

    std::stringstream buffer{};
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string compactJsonText(const std::string& inputText) {
    std::string outputText{};
    outputText.reserve(inputText.size());

    bool inString = false;
    bool escaping = false;
    for (const char characterValue : inputText) {
        if (inString) {
            outputText.push_back(characterValue);
            if (escaping) {
                escaping = false;
                continue;
            }
            if (characterValue == '\\') {
                escaping = true;
                continue;
            }
            if (characterValue == '"') {
                inString = false;
            }
            continue;
        }

        if (characterValue == '"') {
            inString = true;
            outputText.push_back(characterValue);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(characterValue)) == 0) {
            outputText.push_back(characterValue);
        }
    }

    return outputText;
}

[[nodiscard]] std::filesystem::path resolveOracleDataRootPath() {
    const std::vector<std::filesystem::path> candidates{
        std::filesystem::path{"test/oracle/serialdevice/data"},
        std::filesystem::path{"../test/oracle/serialdevice/data"},
        std::filesystem::path{"../../test/oracle/serialdevice/data"},
        std::filesystem::path{__FILE__}.parent_path() / "../../../../test/oracle/serialdevice/data",
        std::filesystem::path{__FILE__}.parent_path() / "../../../../../test/oracle/serialdevice/data"
    };

    for (const auto& candidatePath : candidates) {
        if (std::filesystem::exists(candidatePath)) {
            return std::filesystem::canonical(candidatePath);
        }
    }

    throw std::runtime_error{"Unable to locate Oracle B/C fixtures"};
}

[[nodiscard]] yaha::SerialDeviceConfig makeDefaultOracleConfig() {
    yaha::SerialDeviceConfig config{};
    config.subscribeQos = yaha::Qos::AtLeastOnce;
    config.traceLevel = "errors";
    config.keepAliveDelayInSeconds = 1;
    config.serialPortName = "/dev/null";
    config.baudrate = yaha::k_default_serialdevice_baudrate;

    yaha::SerialDeviceInterfaceDefinition i2cDefinition{};
    i2cDefinition.commandMap["L"] = "i2c/brightness sensor/brightness";
    i2cDefinition.commandMap["M"] = "i2c/motion sensor/detection state";
    i2cDefinition.commandMap["T"] = "i2c/temperature and humidity sensor/temperature in celsius";
    i2cDefinition.receiverMap["level0/room1/device1/"] = "3";
    i2cDefinition.receiverMap["level1/room1/device1/"] = "4";
    i2cDefinition.receiverMap["$SYS/central/"] = "main";
    i2cDefinition.receiverMapProvided = true;

    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["12322324/2111"] = "level0/room1/fs20/task/one";
    fs20Definition.sendMap["12322324/2112"] = "level0/room1/fs20/task/two";

    yaha::SerialDeviceInterfaceDefinition switchDefinition{};
    switchDefinition.topicMap["level0/room1/switch/one"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "switch",
        .value = 1U,
        .address = "main"};
    switchDefinition.topicMap["level0/room1/switch/two"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "switch",
        .value = 2U,
        .address = "main"};

    yaha::SerialDeviceInterfaceDefinition serialDefinition{};
    serialDefinition.commandMap["t"] = "Temperature and Humidity Sensor/temperature in celsius";
    serialDefinition.commandMap["l"] = "Light/light on time";
    serialDefinition.commandMap["z"] = "Arduino Status Information/memory left in bytes";
    serialDefinition.receiverMap["level0/room1/device1/"] = "5";
    serialDefinition.receiverMap["level0/room1/device2/"] = "6";
    serialDefinition.receiverMapProvided = true;
    serialDefinition.valueMap["LightOnOff"] = yaha::SerialDeviceValueMapDefinition{
        .description = "Switches light on/off by setting the light on time in seconds",
        .usedBy = {"V", "l"},
        .map = {{"on", k_light_on_time_seconds}, {"off", 0U}}};

    config.interfaces["i2c"] = i2cDefinition;
    config.interfaces["fs20"] = fs20Definition;
    config.interfaces["switch"] = switchDefinition;
    config.interfaces["serial"] = serialDefinition;
    return config;
}

[[nodiscard]] yaha::SerialDeviceEndpoint endpointFromJson(const mqtt::json::JsonValue& value) {
    if (value.is_null()) {
        return yaha::SerialDeviceEndpoint{};
    }
    if (value.is_number()) {
        return yaha::SerialDeviceEndpoint{static_cast<std::int64_t>(value.as_number())};
    }
    return yaha::SerialDeviceEndpoint{value.as_string()};
}

[[nodiscard]] yaha::SerialDeviceValue payloadFromJson(const mqtt::json::JsonValue& value) {
    if (value.is_number()) {
        return yaha::SerialDeviceValue{static_cast<std::int64_t>(value.as_number())};
    }
    return yaha::SerialDeviceValue{value.as_string()};
}

[[nodiscard]] yaha::SerialDeviceMessage serialMessageFromJson(const mqtt::json::JsonValue& value) {
    yaha::SerialDeviceMessage output{};
    output.interfaceName = value.at("interfaceName").as_string();
    output.sender = endpointFromJson(value.at("sender"));
    output.receiver = endpointFromJson(value.at("receiver"));
    output.command = value.at("command").as_string();
    output.value = payloadFromJson(value.at("value"));
    output.action = value.at("action").as_string();
    return output;
}

struct OracleMqttMessage {
    std::string topic{};
    std::variant<std::int64_t, std::string> value{};
    std::string reason{};
    std::uint32_t qos{0U};

    [[nodiscard]] bool operator==(const OracleMqttMessage& otherValue) const = default;
};

[[nodiscard]] OracleMqttMessage oracleMqttMessageFromJson(const mqtt::json::JsonValue& value) {
    OracleMqttMessage output{};
    output.topic = value.at("topic").as_string();
    output.reason = value.at("reason").as_string();
    output.qos = static_cast<std::uint32_t>(value.at("qos").as_number());
    if (value.at("value").is_number()) {
        output.value = static_cast<std::int64_t>(value.at("value").as_number());
    } else {
        output.value = value.at("value").as_string();
    }
    return output;
}

[[nodiscard]] OracleMqttMessage oracleMqttMessageFromDomain(const yaha::Message& value) {
    OracleMqttMessage output{};
    output.topic = value.topic();
    output.reason = value.reason().empty() ? "" : value.reason().front().message;
    output.qos = static_cast<std::uint32_t>(value.qos());

    if (std::holds_alternative<std::string>(value.value())) {
        output.value = std::get<std::string>(value.value());
    } else {
        output.value = static_cast<std::int64_t>(std::get<double>(value.value()));
    }

    return output;
}

void verifyMqttToSerialSuccessSuite(
    const yaha::SerialDeviceConfig& config,
    const std::filesystem::path& fixturePath) {
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    for (const auto& caseValue : fixtureJson.at("cases").as_array()) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        INFO("case=" << caseIdentifier);

        const std::string topicValue = caseValue.at("input").at("topic").as_string();
        const std::string inputValue = caseValue.at("input").at("value").as_string();

        REQUIRE(caseValue.contains("expected"));
        const yaha::SerialDeviceMessage expectedMessage = serialMessageFromJson(caseValue.at("expected"));
        const yaha::SerialDeviceMessage actualMessage = yaha::mapMqttToSerialMessage(config, topicValue, inputValue);
        CHECK(actualMessage == expectedMessage);
    }
}

void verifyMqttToSerialErrorSuite(
    const yaha::SerialDeviceConfig& config,
    const std::filesystem::path& fixturePath) {
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    for (const auto& caseValue : fixtureJson.at("cases").as_array()) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        INFO("case=" << caseIdentifier);

        const std::string topicValue = caseValue.at("input").at("topic").as_string();
        const std::string inputValue = caseValue.at("input").at("value").as_string();
        REQUIRE(caseValue.contains("expectedError"));

        const std::string expectedError = caseValue.at("expectedError").as_string();
        try {
            static_cast<void>(yaha::mapMqttToSerialMessage(config, topicValue, inputValue));
            FAIL("expected mapper to throw: " << expectedError);
        } catch (const std::runtime_error& exception) {
            CHECK(std::string{exception.what()} == expectedError);
        }
    }
}

void verifySerialToMqttSuite(
    const yaha::SerialDeviceConfig& config,
    const std::filesystem::path& fixturePath) {
    const std::string fixtureText = readTextFile(fixturePath);
    const mqtt::json::JsonValue fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(fixtureText));

    for (const auto& caseValue : fixtureJson.at("cases").as_array()) {
        const std::string caseIdentifier = caseValue.at("id").as_string();
        INFO("case=" << caseIdentifier);

        const yaha::SerialDeviceMessage inputMessage = serialMessageFromJson(caseValue.at("input"));
        const std::vector<yaha::Message> actualMessages = yaha::mapSerialMessageToMqttMessages(config, inputMessage);

        std::vector<OracleMqttMessage> expectedMessages{};
        for (const auto& messageValue : caseValue.at("expected").as_array()) {
            expectedMessages.push_back(oracleMqttMessageFromJson(messageValue));
        }

        std::vector<OracleMqttMessage> actualNormalized{};
        actualNormalized.reserve(actualMessages.size());
        for (const auto& messageValue : actualMessages) {
            actualNormalized.push_back(oracleMqttMessageFromDomain(messageValue));
        }

        REQUIRE(actualNormalized.size() == expectedMessages.size());
        for (std::size_t indexValue = 0; indexValue < expectedMessages.size(); ++indexValue) {
            CHECK(actualNormalized.at(indexValue) == expectedMessages.at(indexValue));
        }
    }
}

} // namespace

TEST_CASE("serial_device_mqtt_to_serial_matches_oracle_b_fixtures", "[serial_device][parity]") {
    const yaha::SerialDeviceConfig config = makeDefaultOracleConfig();
    const std::filesystem::path rootPath = resolveOracleDataRootPath();

    verifyMqttToSerialSuccessSuite(config, rootPath / "B-mqtt-to-serial/core.json");
    verifyMqttToSerialErrorSuite(config, rootPath / "B-mqtt-to-serial/errors.json");
}

TEST_CASE("serial_device_serial_to_mqtt_matches_oracle_c_fixtures", "[serial_device][parity]") {
    const yaha::SerialDeviceConfig config = makeDefaultOracleConfig();
    const std::filesystem::path rootPath = resolveOracleDataRootPath();

    verifySerialToMqttSuite(config, rootPath / "C-serial-to-mqtt/core.json");
    verifySerialToMqttSuite(config, rootPath / "C-serial-to-mqtt/switch.json");
}
