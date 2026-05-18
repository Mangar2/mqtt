#include <catch2/catch_test_macros.hpp>

#include "yaha/error_handling/yaha_error.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_rs485_runtime_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

bool loadRuntimeConfigFromIniText(
    const std::string& iniText,
    yaha::Rs485InterfaceRuntimeConfig& output,
    std::string& errorMessage) {
    const auto iniPath = writeTempIni(iniText);

    yaha::IniDocument document{};
    try {
        document = yaha::IniDocument::loadFromFile(iniPath);
    } catch (const std::exception& exceptionValue) {
        errorMessage = exceptionValue.what();
        std::filesystem::remove(iniPath);
        return false;
    }

    try {
        output = yaha::loadRs485InterfaceClientRuntimeConfigFromIni(document);
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
        std::filesystem::remove(iniPath);
        return false;
    }
    std::filesystem::remove(iniPath);
    return true;
}

} // namespace

[[nodiscard]] std::string validRuntimeIniText() {
    return
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=rs485-client\n"
        "\n"
        "[rs485interface]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "baudrate=115200\n"
        "myAddress=7\n"
        "maxVersion=1\n"
        "tickDelay=150\n"
        "timeOfDayDelayInSeconds=30\n"
        "qos=2\n"
        "trace=internal\n"
        "logIncomingMessages=true\n"
        "logOutgoingMessages=false\n"
        "blinkDelayInSeconds=4\n"
        "temporaryOnInSeconds=20\n"
        "\n"
        "[rs485interface.interfaces]\n"
        "LightOnOff=usedby=V;map=on:3600|off:0\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "X=switch/status\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "o=window/detection state\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n"
        "\n"
        "[rs485interface.topics]\n"
        "my/floor/device/switch/s1=X,1,20\n";
}

[[nodiscard]] yaha::Rs485InterfaceRuntimeConfig loadValidRuntimeConfig() {
    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    REQUIRE(loadRuntimeConfigFromIniText(validRuntimeIniText(), runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    return runtimeConfig;
}

TEST_CASE("rs485_runtime_config_parses_rs485_core_transport_fields", "[rs485_interface]") {
    const auto runtimeConfig = loadValidRuntimeConfig();

    REQUIRE(runtimeConfig.rs485Config.serialPortName == "/dev/ttyUSB0");
    REQUIRE(runtimeConfig.rs485Config.baudrate == 115200U);
    REQUIRE(runtimeConfig.rs485Config.myAddress == 7U);
    REQUIRE(runtimeConfig.rs485Config.maxVersion == 1U);
    REQUIRE(runtimeConfig.rs485Config.tickDelayMs == 150U);
    REQUIRE(runtimeConfig.rs485Config.timeOfDayDelaySeconds == 30U);
}

TEST_CASE("rs485_runtime_config_parses_rs485_behavior_fields", "[rs485_interface]") {
    const auto runtimeConfig = loadValidRuntimeConfig();

    REQUIRE(runtimeConfig.rs485Config.subscribeQos == yaha::Qos::ExactlyOnce);
    REQUIRE(runtimeConfig.rs485Config.traceLevel == "internal");
    REQUIRE(runtimeConfig.rs485Config.logIncomingMessages == true);
    REQUIRE(runtimeConfig.rs485Config.logOutgoingMessages == false);
    REQUIRE(runtimeConfig.rs485Config.blinkDelaySeconds == 4U);
    REQUIRE(runtimeConfig.rs485Config.temporaryOnSeconds == 20U);
}

TEST_CASE("rs485_runtime_config_parses_mqtt_connection_fields", "[rs485_interface]") {
    const auto runtimeConfig = loadValidRuntimeConfig();

    REQUIRE(runtimeConfig.mqttConfig.brokerHost == "127.0.0.1");
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == 1883U);
    REQUIRE(runtimeConfig.mqttConfig.clientId == "rs485-client");
    REQUIRE(runtimeConfig.mqttConfig.enableMessageTrace == true);
}

TEST_CASE("rs485_runtime_config_parses_interfaces_and_value_map", "[rs485_interface]") {
    const auto runtimeConfig = loadValidRuntimeConfig();

    REQUIRE(runtimeConfig.rs485Config.interfaces.contains("LightOnOff"));
    REQUIRE(runtimeConfig.rs485Config.interfaces.at("LightOnOff").usedBy.size() == 1U);
    REQUIRE(runtimeConfig.rs485Config.interfaces.at("LightOnOff").usedBy.front() == 'V');
    REQUIRE(runtimeConfig.rs485Config.interfaces.at("LightOnOff").map.at("on") == 3600U);
    REQUIRE(runtimeConfig.rs485Config.interfaces.at("LightOnOff").map.at("off") == 0U);
}

TEST_CASE("rs485_runtime_config_parses_command_address_and_topic_sections", "[rs485_interface]") {
    const auto runtimeConfig = loadValidRuntimeConfig();

    REQUIRE(runtimeConfig.rs485Config.settings.at('V') == "light/light on time");
    REQUIRE(runtimeConfig.rs485Config.status.at('v') == "light/light voltage");
    REQUIRE(runtimeConfig.rs485Config.addresses.at("my/floor/device/") == 20U);
    REQUIRE(runtimeConfig.rs485Config.topics.at("my/floor/device/switch/s1").command == 'X');
    REQUIRE(runtimeConfig.rs485Config.topics.at("my/floor/device/switch/s1").value == 1U);
    REQUIRE(runtimeConfig.rs485Config.topics.at("my/floor/device/switch/s1").address == 20U);
}

TEST_CASE("rs485_runtime_config_rejects_missing_serial_port", "[rs485_interface]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[rs485interface]\n"
        "baudrate=115200\n"
        "\n"
        "[rs485interface.interfaces]\n"
        "LightOnOff=usedby=V;map=on:3600|off:0\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n";

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(loadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("rs485interface.serialPortName") != std::string::npos);
}

TEST_CASE("rs485_runtime_config_rejects_invalid_trace_value", "[rs485_interface]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[rs485interface]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "trace=error\n"
        "\n"
        "[rs485interface.interfaces]\n"
        "LightOnOff=usedby=V;map=on:3600|off:0\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n";

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(loadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("rs485interface.trace") != std::string::npos);
}

TEST_CASE("rs485_runtime_config_rejects_missing_required_interfaces_section", "[rs485_interface]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[rs485interface]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n";

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(loadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("[rs485interface.interfaces]") != std::string::npos);
}

TEST_CASE("rs485_runtime_config_rejects_invalid_topic_mapping_format", "[rs485_interface]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[rs485interface]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "\n"
        "[rs485interface.interfaces]\n"
        "LightOnOff=usedby=V;map=on:3600|off:0\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n"
        "\n"
        "[rs485interface.topics]\n"
        "my/floor/device/switch/s1=INVALID\n";

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(loadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("[rs485interface.topics]") != std::string::npos);
}

TEST_CASE("rs485_runtime_config_rejects_invalid_interface_map_value", "[rs485_interface]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[rs485interface]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "\n"
        "[rs485interface.interfaces]\n"
        "LightOnOff=usedby=V;map=on:3600|off:x\n"
        "\n"
        "[rs485interface.settings]\n"
        "V=light/light on time\n"
        "\n"
        "[rs485interface.status]\n"
        "v=light/light voltage\n"
        "\n"
        "[rs485interface.addresses]\n"
        "my/floor/device/=20\n";

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(loadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("[rs485interface.interfaces]") != std::string::npos);
}
