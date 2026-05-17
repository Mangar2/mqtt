#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_interface/rs485_interface_component.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"

#include <chrono>
#include <string>
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

[[nodiscard]] yaha::Rs485InterfaceRuntimeConfig makeRuntimeConfig() {
    yaha::Rs485InterfaceRuntimeConfig config{};

    config.mqttConfig.brokerHost = "127.0.0.1";
    config.mqttConfig.brokerPort = k_default_mqtt_port;
    config.mqttConfig.clientId = "rs485-runtime-test";
    config.mqttConfig.keepAliveInterval = std::chrono::seconds{k_keep_alive_seconds};
    config.mqttConfig.reconnectDelay = std::chrono::milliseconds{k_reconnect_delay_ms};
    config.mqttConfig.loopSleep = std::chrono::milliseconds{k_loop_sleep_ms};

    config.rs485Config.serialPortName = "/dev/null";
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

TEST_CASE("rs485_runtime_build_creates_component_adapter_mqtt_runtime_objects", "[rs485_interface]") {
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    const bool success = yaha::tryBuildRs485InterfaceClientRuntime(
        makeRuntimeConfig(),
        runtimeObjects,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeObjects.component != nullptr);
    REQUIRE(runtimeObjects.serialAdapter != nullptr);
    REQUIRE(runtimeObjects.mqttClient != nullptr);
    REQUIRE(runtimeObjects.runtime != nullptr);
}

TEST_CASE("rs485_runtime_component_startup_and_shutdown_is_clean", "[rs485_interface]") {
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    REQUIRE(yaha::tryBuildRs485InterfaceClientRuntime(makeRuntimeConfig(), runtimeObjects, errorMessage));

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
