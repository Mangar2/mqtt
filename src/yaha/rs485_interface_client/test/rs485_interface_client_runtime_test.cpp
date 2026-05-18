#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_interface/rs485_interface_component.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"

#include <chrono>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr std::uint16_t k_default_mqtt_port{1883U};
constexpr std::uint32_t k_default_baudrate{57600U};
constexpr std::uint8_t k_my_address{6U};
constexpr std::uint32_t k_tick_delay_ms{5U};
constexpr std::uint32_t k_time_of_day_delay_seconds{3600U};
constexpr std::uint8_t k_device_address{7U};
constexpr std::uint32_t k_serial_test_baudrate{57600U};
constexpr std::int64_t k_keep_alive_seconds{30};
constexpr std::int64_t k_reconnect_delay_ms{1000};
constexpr std::int64_t k_loop_sleep_ms{10};

struct PseudoTerminal {
    PseudoTerminal() = default;
    int masterFd{-1};
    std::string slavePath{};

    ~PseudoTerminal() {
        if (masterFd >= 0) {
            ::close(masterFd);
            masterFd = -1;
        }
    }

    PseudoTerminal(const PseudoTerminal&) = delete;
    PseudoTerminal& operator=(const PseudoTerminal&) = delete;
    PseudoTerminal(PseudoTerminal&&) = delete;
    PseudoTerminal& operator=(PseudoTerminal&&) = delete;
};

[[nodiscard]] yaha::Rs485InterfaceRuntimeConfig makeRuntimeConfig(const std::string& serialPortName);

[[nodiscard]] bool tryCreatePseudoTerminal(PseudoTerminal& output, std::string& errorMessage) {
    const int masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0) {
        errorMessage = "posix_openpt failed";
        return false;
    }

    if (::grantpt(masterFd) != 0) {
        errorMessage = "grantpt failed";
        ::close(masterFd);
        return false;
    }

    if (::unlockpt(masterFd) != 0) {
        errorMessage = "unlockpt failed";
        ::close(masterFd);
        return false;
    }

    const char* slaveName = ::ptsname(masterFd);
    if (slaveName == nullptr) {
        errorMessage = "ptsname failed";
        ::close(masterFd);
        return false;
    }

    output.masterFd = masterFd;
    output.slavePath = slaveName;
    return true;
}

[[nodiscard]] bool tryBuildRuntimeWithPseudoTerminal(
    PseudoTerminal& pseudoTerminal,
    yaha::Rs485InterfaceClientRuntimeObjects& runtimeObjects,
    std::string& errorMessage) {
    std::string pseudoTerminalError{};
    if (!tryCreatePseudoTerminal(pseudoTerminal, pseudoTerminalError)) {
        errorMessage = pseudoTerminalError;
        return false;
    }

    return yaha::tryBuildRs485InterfaceClientRuntime(
        makeRuntimeConfig(pseudoTerminal.slavePath),
        runtimeObjects,
        errorMessage);
}

[[nodiscard]] yaha::Rs485InterfaceRuntimeConfig makeRuntimeConfig(const std::string& serialPortName) {
    yaha::Rs485InterfaceRuntimeConfig config{};

    config.mqttConfig.brokerHost = "127.0.0.1";
    config.mqttConfig.brokerPort = k_default_mqtt_port;
    config.mqttConfig.clientId = "rs485-runtime-test";
    config.mqttConfig.keepAliveInterval = std::chrono::seconds{k_keep_alive_seconds};
    config.mqttConfig.reconnectDelay = std::chrono::milliseconds{k_reconnect_delay_ms};
    config.mqttConfig.loopSleep = std::chrono::milliseconds{k_loop_sleep_ms};

    config.rs485Config.serialPortName = serialPortName;
    config.rs485Config.baudrate = k_default_baudrate;
    config.rs485Config.myAddress = k_my_address;
    config.rs485Config.maxVersion = 1U;
    config.rs485Config.tickDelayMs = k_tick_delay_ms;
    config.rs485Config.timeOfDayDelaySeconds = k_time_of_day_delay_seconds;
    config.rs485Config.traceLevel = "messages";
    config.rs485Config.interfaces["switch"] = yaha::Rs485InterfaceDefinition{
        .usedBy = {'P'},
        .map = {{"on", 1U}, {"off", 0U}}};
    config.rs485Config.settings['P'] = "/power";
    config.rs485Config.status['P'] = "/power";
    config.rs485Config.addresses["house/room/device"] = k_device_address;

    return config;
}

} // namespace

TEST_CASE("rs485_runtime_build_creates_all_runtime_object_pointers", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    const bool success = tryBuildRuntimeWithPseudoTerminal(
        pseudoTerminal,
        runtimeObjects,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeObjects.component != nullptr);
    REQUIRE(runtimeObjects.serialAdapter != nullptr);
    REQUIRE(runtimeObjects.mqttClient != nullptr);
    REQUIRE(runtimeObjects.runtime != nullptr);
}

TEST_CASE("rs485_runtime_build_opens_serial_adapter", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    REQUIRE(tryBuildRuntimeWithPseudoTerminal(pseudoTerminal, runtimeObjects, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeObjects.serialAdapter->isOpen());
}

TEST_CASE("rs485_runtime_build_fails_when_serial_open_fails", "[rs485_interface]") {
    auto runtimeConfig = makeRuntimeConfig("/definitely/not/a/serial/device");

    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    const bool success = yaha::tryBuildRs485InterfaceClientRuntime(
        std::move(runtimeConfig),
        runtimeObjects,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("RS485_RUNTIME_SERIAL_OPEN_FAILED") != std::string::npos);
}

TEST_CASE("rs485_runtime_component_startup_and_shutdown_is_clean", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    REQUIRE(tryBuildRuntimeWithPseudoTerminal(pseudoTerminal, runtimeObjects, errorMessage));

    auto* component = dynamic_cast<yaha::Rs485InterfaceComponent*>(runtimeObjects.component.get());
    REQUIRE(component != nullptr);

    REQUIRE_NOTHROW(component->run());
    REQUIRE_NOTHROW(component->close());
}

TEST_CASE("rs485_serial_adapter_open_fails_for_invalid_path", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    std::string errorMessage{};

    const bool success = adapter.open("/definitely/not/a/serial/device", k_serial_test_baudrate, errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.empty() == false);
    REQUIRE(adapter.isOpen() == false);
}

TEST_CASE("rs485_serial_adapter_send_fails_when_not_open", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    std::string errorMessage{};

    const bool success = adapter.send(std::vector<std::uint8_t>{1U, 2U, 3U}, errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage == "serial interface is not open");
}
