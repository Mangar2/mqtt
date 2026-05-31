#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"
#include "yaha/serial_device_client/serial_device_client_config.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempIni(const std::string& iniText) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto iniPath = std::filesystem::temp_directory_path() /
        ("yaha_serial_device_client_test_" + std::to_string(stamp) + ".ini");
    std::ofstream outputFile{iniPath, std::ios::binary | std::ios::trunc};
    outputFile << iniText;
    return iniPath;
}

bool loadDocumentFromIniText(
    const std::string& iniText,
    yaha::IniDocument& document,
    std::string& errorMessage) {
    const auto iniPath = writeTempIni(iniText);

    try {
        document = yaha::IniDocument::loadFromFile(iniPath);
    } catch (const std::exception& exceptionValue) {
        errorMessage = exceptionValue.what();
        std::filesystem::remove(iniPath);
        return false;
    }

    std::filesystem::remove(iniPath);
    return true;
}

[[nodiscard]] std::string validSerialDeviceIniText() {
    return "[mqtt]\n"
           "host=broker.local\n"
           "port=1884\n"
           "clientId=serial-client\n"
           "\n"
           "[serialdevice]\n"
           "serialPortName=/dev/ttyUSB0\n"
           "baudrate=57600\n"
           "qos=2\n"
           "trace=messages\n"
           "logIncomingMessages=true\n"
           "logOutgoingMessages=true\n"
           "logReason=false\n"
           "keepAliveDelayInSeconds=45\n"
           "\n"
           "[serialdevice.i2c.commandMap]\n"
           "temp=sensor/temp\n"
           "\n"
           "[serialdevice.i2c.receiverMap]\n"
           "home/living/=A1\n"
           "\n"
           "[serialdevice.fs20.commandMap]\n"
           "switchon=lights/on\n"
           "\n"
           "[serialdevice.fs20.sendMap]\n"
           "A1=lights/send\n"
           "\n"
           "[serialdevice.switch.topicMap]\n"
           "home/switch/s1=switch,1,AB\n"
           "\n"
           "[serialdevice.serial.commandMap]\n"
           "state=serial/state\n"
           "\n"
           "[serialdevice.serial.receiverMap]\n"
           "home/serial/=RX1\n"
           "\n"
           "[serialdevice.serial.valueMap]\n"
           "stateMap=description=state values;usedby=state;map=off:0|on:1\n";
}

[[nodiscard]] yaha::IniDocument loadDocumentOrFail(const std::string& iniText) {
    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocumentFromIniText(iniText, document, errorMessage));
    REQUIRE(errorMessage.empty());
    return document;
}

} // namespace

TEST_CASE("serial_device_config_rejects_missing_serial_port_name", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(
        "[serialdevice]\n"
        "baudrate=38400\n");

    yaha::SerialDeviceConfig config{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceConfigFromIni(document, config, errorMessage);

    REQUIRE_FALSE(loaded);
    REQUIRE(errorMessage.find("serialdevice.serialPortName") != std::string::npos);
}

TEST_CASE("serial_device_config_applies_defaults_for_invalid_numeric_values", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(
        "[serialdevice]\n"
        "serialPortName=/dev/ttyUSB1\n"
        "baudrate=not-a-number\n"
        "qos=99\n"
        "keepAliveDelayInSeconds=-1\n");

    yaha::SerialDeviceConfig config{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.baudrate == yaha::k_default_serialdevice_baudrate);
    REQUIRE(config.subscribeQos == yaha::Qos::AtLeastOnce);
    REQUIRE(config.keepAliveDelayInSeconds == yaha::k_default_serialdevice_keep_alive_delay_seconds);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_config_parses_interface_mappings_and_value_map", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(validSerialDeviceIniText());

    yaha::SerialDeviceConfig config{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.serialPortName == "/dev/ttyUSB0");
    REQUIRE(config.baudrate == 57600U);
    REQUIRE(config.subscribeQos == yaha::Qos::ExactlyOnce);
    REQUIRE(config.traceLevel == "messages");
    REQUIRE(config.logIncomingMessages);
    REQUIRE(config.logOutgoingMessages);
    REQUIRE_FALSE(config.logReason);
    REQUIRE(config.keepAliveDelayInSeconds == 45U);

    REQUIRE(config.interfaces.at("i2c").commandMap.at("temp") == "sensor/temp");
    REQUIRE(config.interfaces.at("i2c").receiverMap.at("home/living/") == "A1");
    REQUIRE(config.interfaces.at("fs20").commandMap.at("switchon") == "lights/on");
    REQUIRE(config.interfaces.at("fs20").sendMap.at("A1") == "lights/send");

    REQUIRE(config.interfaces.at("switch").topicMap.at("home/switch/s1").command == "switch");
    REQUIRE(config.interfaces.at("switch").topicMap.at("home/switch/s1").value == 1U);
    REQUIRE(config.interfaces.at("switch").topicMap.at("home/switch/s1").address == "AB");

    REQUIRE(config.interfaces.at("serial").commandMap.at("state") == "serial/state");
    REQUIRE(config.interfaces.at("serial").receiverMap.at("home/serial/") == "RX1");
    REQUIRE(config.interfaces.at("serial").valueMap.at("stateMap").description == "state values");
    REQUIRE(config.interfaces.at("serial").valueMap.at("stateMap").usedBy.size() == 1U);
    REQUIRE(config.interfaces.at("serial").valueMap.at("stateMap").usedBy.front() == "state");
    REQUIRE(config.interfaces.at("serial").valueMap.at("stateMap").map.at("on") == 1U);
}

TEST_CASE("serial_device_config_marks_receiver_map_as_provided_for_empty_section", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(
        "[serialdevice]\n"
        "serialPortName=/dev/ttyUSB2\n"
        "\n"
        "[serialdevice.serial.receiverMap]\n");

    yaha::SerialDeviceConfig config{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.interfaces.at("serial").receiverMapProvided);
    REQUIRE(config.interfaces.at("serial").receiverMap.empty());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_runtime_config_loads_domain_and_mqtt_values", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(validSerialDeviceIniText());

    yaha::SerialDeviceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.serialDeviceConfig.serialPortName == "/dev/ttyUSB0");
    REQUIRE(runtimeConfig.mqttConfig.brokerHost == "broker.local");
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == 1884U);
    REQUIRE(runtimeConfig.mqttConfig.clientId == "serial-client");
    REQUIRE_FALSE(runtimeConfig.mqttConfig.logReason);
}

TEST_CASE("serial_device_config_falls_back_on_invalid_message_logging_bools", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(
        "[serialdevice]\n"
        "serialPortName=/dev/ttyUSB4\n"
        "logIncomingMessages=maybe\n"
        "logOutgoingMessages=maybe\n"
        "logReason=maybe\n");

    yaha::SerialDeviceConfig config{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(config.logIncomingMessages);
    REQUIRE_FALSE(config.logOutgoingMessages);
    REQUIRE(config.logReason);
}

TEST_CASE("serial_device_runtime_config_keeps_mqtt_defaults_on_invalid_mqtt_values", "[serial_device_client]") {
    const yaha::IniDocument document = loadDocumentOrFail(
        "[mqtt]\n"
        "host=broker.local\n"
        "port=invalid\n"
        "\n"
        "[serialdevice]\n"
        "serialPortName=/dev/ttyUSB3\n");

    yaha::SerialDeviceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool loaded = yaha::tryLoadSerialDeviceClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.mqttConfig.brokerHost == "broker.local");
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == 1883U);
}
