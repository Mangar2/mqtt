#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"
#include "yaha/zwave_client/zwave_client_app.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path makeTemporaryDirectory() {
    const auto tickValue = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directoryPath =
        std::filesystem::temp_directory_path() / ("yaha_zwave_client_test_" + std::to_string(tickValue));
    std::filesystem::create_directories(directoryPath);
    return directoryPath;
}

void removeDirectoryQuiet(const std::filesystem::path& directoryPath) {
    std::error_code errorCode{};
    std::filesystem::remove_all(directoryPath, errorCode);
}

[[nodiscard]] std::filesystem::path writeIniFile(
    const std::filesystem::path& directoryPath,
    const std::string& contentText) {
    const std::filesystem::path filePath = directoryPath / "zwave.ini";
    std::ofstream output{filePath};
    output << contentText;
    return filePath;
}

[[nodiscard]] yaha::IniDocument loadIni(const std::string& iniText) {
    const std::filesystem::path tempDirectory = makeTemporaryDirectory();
    const std::filesystem::path iniPath = writeIniFile(tempDirectory, iniText);
    yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);
    removeDirectoryQuiet(tempDirectory);
    return document;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_applies_defaults_and_parses_required_device", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.subscribeQos == yaha::Qos::AtLeastOnce);
    CHECK(config.qos == yaha::Qos::AtLeastOnce);
    CHECK_FALSE(config.retain);
    CHECK(config.usb.device == "/dev/ttyUSB0");
    CHECK(config.usb.topic == "home/zwave/controller");
    REQUIRE(config.devices.size() == 1U);
    CHECK(config.devices.front().topic == "home/lamp");
    CHECK(config.devices.front().nodeId == 7U);
    CHECK_FALSE(config.devices.front().classId.has_value());
}

TEST_CASE("load_zwave_config_rejects_invalid_device_row", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|500\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage.find("nodeId must be in range 1..255") != std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_invalid_device_field_count", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage.find("invalid zwave.device entry") != std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_empty_device_topic", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage.find("topic must not be empty") != std::string::npos);
}

TEST_CASE("load_zwave_config_rejects_invalid_optional_numeric_fields", "[zwave_client]") {
    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7|bad\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("classId") != std::string::npos);
    }

    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7|37|300\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("instance") != std::string::npos);
    }

    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7|37|1|300\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("index") != std::string::npos);
    }
}

TEST_CASE("load_zwave_config_rejects_invalid_qos_and_retain_values", "[zwave_client]") {
    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "subscribeQoS=9\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("zwave.subscribeQoS") != std::string::npos);
    }

    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "qos=9\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("zwave.qos") != std::string::npos);
    }

    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "retain=maybe\n"
            "usbDevice=/dev/ttyUSB0\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("zwave.retain") != std::string::npos);
    }
}

TEST_CASE("load_zwave_config_parses_log_message_flags", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "logIncomingMessages=true\n"
        "logOutgoingMessages=true\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.logIncomingMessages);
    CHECK(config.logOutgoingMessages);
}

TEST_CASE("load_zwave_config_rejects_invalid_log_outgoing_messages_value", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "logOutgoingMessages=maybe\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|7\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage.find("zwave.logOutgoingMessages") != std::string::npos);
}

TEST_CASE("load_zwave_config_requires_usb_settings", "[zwave_client]") {
    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "usbTopic=home/zwave/controller\n"
            "device=home/lamp|7\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("zwave.usbDevice") != std::string::npos);
    }

    {
        const yaha::IniDocument document = loadIni(
            "[zwave]\n"
            "usbDevice=/dev/ttyUSB0\n"
            "device=home/lamp|7\n");

        yaha::ZwaveConfig config{};
        std::string errorMessage{};
        CHECK_FALSE(yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage));
        CHECK(errorMessage.find("zwave.usbTopic") != std::string::npos);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_runtime_config_combines_zwave_and_mqtt_sections", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[mqtt]\n"
        "host=broker.local\n"
        "port=1885\n"
        "clientId=zwave-client\n"
        "reconnectDelayMs=2000\n"
        "keepAliveIntervalMs=40000\n"
        "loopSleepMs=50\n"
        "logReason=false\n"
        "\n"
        "[zwave]\n"
        "subscribeQoS=2\n"
        "qos=0\n"
        "retain=true\n"
        "usbDevice=/dev/ttyUSB9\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|9|37|1|0|switch|power\n");

    yaha::ZwaveClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());

    CHECK(runtimeConfig.zwaveConfig.subscribeQos == yaha::Qos::ExactlyOnce);
    CHECK(runtimeConfig.zwaveConfig.qos == yaha::Qos::AtMostOnce);
    CHECK(runtimeConfig.zwaveConfig.retain);
    CHECK(runtimeConfig.zwaveConfig.usb.device == "/dev/ttyUSB9");
    CHECK(runtimeConfig.zwaveConfig.usb.topic == "home/zwave/controller");
    REQUIRE(runtimeConfig.zwaveConfig.devices.size() == 1U);
    CHECK(runtimeConfig.zwaveConfig.devices.front().topic == "home/lamp");
    CHECK(runtimeConfig.zwaveConfig.devices.front().nodeId == 9U);
    REQUIRE(runtimeConfig.zwaveConfig.devices.front().classId.has_value());
    CHECK(runtimeConfig.zwaveConfig.devices.front().classId.value() == 37U);

    CHECK(runtimeConfig.mqttConfig.brokerHost == "broker.local");
    CHECK(runtimeConfig.mqttConfig.brokerPort == 1885U);
    CHECK(runtimeConfig.mqttConfig.clientId == "zwave-client");
    CHECK(runtimeConfig.mqttConfig.reconnectDelay.count() == 2000);
    CHECK(runtimeConfig.mqttConfig.keepAliveInterval.count() == 40000);
    CHECK(runtimeConfig.mqttConfig.loopSleep.count() == 50);
    CHECK_FALSE(runtimeConfig.mqttConfig.logReason);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_zwave_config_parses_legacy_json_equivalent_device_rows", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/zwave\n"
        "usbTopic=$SYS/zwave/usb stick\n"
        "device=first/dressingroom/zwave/sys/dressing room|24\n"
        "device=ground/livingroom/zwave/shutter/southwest|13|38\n"
        "device=first/study/zwave/sys/pcvolker|22||2\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    REQUIRE(loaded);
    CHECK(errorMessage.empty());
    CHECK(config.usb.device == "/dev/zwave");
    CHECK(config.usb.topic == "$SYS/zwave/usb stick");
    REQUIRE(config.devices.size() == 3U);

    CHECK(config.devices[0].topic == "first/dressingroom/zwave/sys/dressing room");
    CHECK(config.devices[0].nodeId == 24U);
    CHECK_FALSE(config.devices[0].classId.has_value());

    CHECK(config.devices[1].topic == "ground/livingroom/zwave/shutter/southwest");
    REQUIRE(config.devices[1].classId.has_value());
    CHECK(config.devices[1].classId.value() == 38U);
    CHECK_FALSE(config.devices[1].type.has_value());

    CHECK(config.devices[2].topic == "first/study/zwave/sys/pcvolker");
    REQUIRE(config.devices[2].instance.has_value());
    CHECK(config.devices[2].instance.value() == 2U);
}

TEST_CASE("load_zwave_runtime_config_reports_mqtt_validation_error", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[mqtt]\n"
        "port=70000\n"
        "\n"
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n"
        "device=home/lamp|7\n");

    yaha::ZwaveClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage.find("mqtt.port") != std::string::npos);
}

TEST_CASE("load_zwave_config_requires_device_setting", "[zwave_client]") {
    const yaha::IniDocument document = loadIni(
        "[zwave]\n"
        "usbDevice=/dev/ttyUSB0\n"
        "usbTopic=home/zwave/controller\n");

    yaha::ZwaveConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadZwaveConfigFromIni(document, config, errorMessage);

    CHECK_FALSE(loaded);
    CHECK(errorMessage == "missing required setting 'zwave.device'");
}
