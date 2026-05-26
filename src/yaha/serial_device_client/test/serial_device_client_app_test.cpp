#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"
#include "yaha/serial_device_client/serial_device_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempIni(const std::string& iniText) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto iniPath = std::filesystem::temp_directory_path() /
        ("yaha_serial_device_client_app_test_" + std::to_string(stamp) + ".ini");
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

[[nodiscard]] yaha::SerialDeviceClientRuntimeConfig loadRuntimeConfigOrFail() {
    const std::string iniText =
        "[mqtt]\n"
        "host=broker.local\n"
        "port=1884\n"
        "clientId=serialdevice-client\n"
        "\n"
        "[serialdevice]\n"
        "serialPortName=/dev/ttyUSB0\n"
        "baudrate=38400\n"
        "qos=1\n"
        "trace=messages\n"
        "keepAliveDelayInSeconds=30\n"
        "\n"
        "[serialdevice.i2c.commandMap]\n"
        "L=demo/topic\n"
        "\n"
        "[serialdevice.i2c.receiverMap]\n"
        "demo/=3\n";

    yaha::IniDocument document{};
    std::string loadError{};
    REQUIRE(loadDocumentFromIniText(iniText, document, loadError));
    REQUIRE(loadError.empty());

    yaha::SerialDeviceClientRuntimeConfig runtimeConfig{};
    std::string parseError{};
    REQUIRE(yaha::tryLoadSerialDeviceClientRuntimeConfigFromIni(document, runtimeConfig, parseError));
    REQUIRE(parseError.empty());

    return runtimeConfig;
}

} // namespace

TEST_CASE("serial_device_client_runtime_build_creates_runtime_objects", "[serial_device_client]") {
    const yaha::SerialDeviceClientRuntimeConfig runtimeConfig = loadRuntimeConfigOrFail();

    yaha::SerialDeviceClientRuntimeObjects runtimeObjects = yaha::buildSerialDeviceClientRuntime(runtimeConfig);

    REQUIRE(runtimeObjects.component != nullptr);
    REQUIRE(runtimeObjects.serialTransport != nullptr);
    REQUIRE(runtimeObjects.mqttClient != nullptr);
    REQUIRE(runtimeObjects.runtime != nullptr);
    CHECK(runtimeObjects.runtimeConfig.serialDeviceConfig.serialPortName == "/dev/ttyUSB0");
    CHECK(runtimeObjects.runtimeConfig.mqttConfig.clientId == "serialdevice-client");
}

TEST_CASE("serial_device_client_runtime_component_derives_system_subscription", "[serial_device_client]") {
    const yaha::SerialDeviceClientRuntimeConfig runtimeConfig = loadRuntimeConfigOrFail();

    yaha::SerialDeviceClientRuntimeObjects runtimeObjects = yaha::buildSerialDeviceClientRuntime(runtimeConfig);
    const yaha::SubscriptionMap subscriptions = runtimeObjects.component->getSubscriptions();

    REQUIRE(subscriptions.contains("$SYS/serialdevice/#"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_client_runtime_transport_methods_are_callable", "[serial_device_client]") {
    const yaha::SerialDeviceClientRuntimeConfig runtimeConfig = loadRuntimeConfigOrFail();
    yaha::SerialDeviceClientRuntimeObjects runtimeObjects = yaha::buildSerialDeviceClientRuntime(runtimeConfig);

    bool callbackInvoked = false;
    runtimeObjects.serialTransport->setReceiveCallback([&callbackInvoked](const std::vector<std::uint8_t>&) {
        callbackInvoked = true;
    });

    runtimeObjects.serialTransport->listAvailablePorts();
    CHECK_FALSE(runtimeObjects.serialTransport->isOpen());

    REQUIRE_THROWS_AS(runtimeObjects.serialTransport->sendData("payload"), std::exception);
    REQUIRE_THROWS_AS(
        runtimeObjects.serialTransport->open("/definitely/missing/serial/device", 38400U),
        std::exception);

    runtimeObjects.serialTransport->close();
    CHECK_FALSE(callbackInvoked);
}
