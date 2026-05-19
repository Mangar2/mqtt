#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_interface/rs485_interface_component.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"
#include "yaha/error_handling/yaha_error.h"


#include <chrono>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <thread>
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
constexpr int k_callback_wait_attempts{30};
constexpr int k_callback_wait_sleep_ms{10};
constexpr std::uint8_t k_send_error_payload_byte_1{0x11U};
constexpr std::uint8_t k_send_error_payload_byte_2{0x22U};
constexpr std::uint32_t k_baudrate_1200{1200U};
constexpr std::uint32_t k_baudrate_2400{2400U};
constexpr std::uint32_t k_baudrate_4800{4800U};
constexpr std::uint32_t k_baudrate_9600{9600U};
constexpr std::uint32_t k_baudrate_19200{19200U};
constexpr std::uint32_t k_baudrate_38400{38400U};
constexpr std::uint32_t k_baudrate_57600{57600U};
#ifdef B115200
constexpr std::uint32_t k_baudrate_115200{115200U};
#endif
#ifdef B230400
constexpr std::uint32_t k_baudrate_230400{230400U};
#endif

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

[[nodiscard]] bool createPseudoTerminal(PseudoTerminal& output, std::string& errorMessage) {
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

[[nodiscard]] bool buildRuntimeWithPseudoTerminal(
    PseudoTerminal& pseudoTerminal,
    yaha::Rs485InterfaceClientRuntimeObjects& runtimeObjects,
    std::string& errorMessage) {
    std::string pseudoTerminalError{};
    if (!createPseudoTerminal(pseudoTerminal, pseudoTerminalError)) {
        errorMessage = pseudoTerminalError;
        return false;
    }

    try {
        runtimeObjects = yaha::buildRs485InterfaceClientRuntime(makeRuntimeConfig(pseudoTerminal.slavePath));
        return true;
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
        return false;
    }
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

    const bool success = buildRuntimeWithPseudoTerminal(
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

    REQUIRE(buildRuntimeWithPseudoTerminal(pseudoTerminal, runtimeObjects, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeObjects.serialAdapter->isOpen());
}

TEST_CASE("rs485_runtime_build_fails_when_serial_open_fails", "[rs485_interface]") {
    auto runtimeConfig = makeRuntimeConfig("/definitely/not/a/serial/device");

    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    try {
        runtimeObjects = yaha::buildRs485InterfaceClientRuntime(std::move(runtimeConfig));
        errorMessage.clear();
        REQUIRE_FALSE(true);
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
    }

    REQUIRE(errorMessage.find("RS485_RUNTIME_SERIAL_OPEN_FAILED") != std::string::npos);
}

TEST_CASE("rs485_runtime_component_startup_and_shutdown_is_clean", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string errorMessage{};

    REQUIRE(buildRuntimeWithPseudoTerminal(pseudoTerminal, runtimeObjects, errorMessage));

    auto* component = dynamic_cast<yaha::Rs485InterfaceComponent*>(runtimeObjects.component.get());
    REQUIRE(component != nullptr);

    REQUIRE_NOTHROW(component->run());
    REQUIRE_NOTHROW(component->close());
}

TEST_CASE("rs485_serial_adapter_open_fails_for_invalid_path", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    std::string errorMessage{};

    REQUIRE_THROWS_AS(adapter.open("/definitely/not/a/serial/device", k_serial_test_baudrate), yaha::YahaError);
    try {
        adapter.open("/definitely/not/a/serial/device", k_serial_test_baudrate);
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
    }
    REQUIRE(errorMessage.empty() == false);
    REQUIRE(adapter.isOpen() == false);
}

TEST_CASE("rs485_serial_adapter_open_fails_for_non_tty_device", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    std::string errorMessage{};

    REQUIRE_THROWS_AS(adapter.open("/dev/null", k_serial_test_baudrate), yaha::YahaError);
    try {
        adapter.open("/dev/null", k_serial_test_baudrate);
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
    }

    REQUIRE_FALSE(errorMessage.empty());
    REQUIRE(errorMessage.find("failed to read serial attributes") != std::string::npos);
    REQUIRE_FALSE(adapter.isOpen());
}

TEST_CASE("rs485_serial_adapter_send_fails_when_not_open", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    std::string errorMessage{};

    REQUIRE_THROWS_AS(adapter.send(std::vector<std::uint8_t>{1U, 2U, 3U}), yaha::YahaError);
    try {
        adapter.send(std::vector<std::uint8_t>{1U, 2U, 3U});
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
    }
    REQUIRE(errorMessage.find("serial interface is not open") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_serial_adapter_send_writes_payload_to_serial_master", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    std::string setupError{};
    REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
    REQUIRE(setupError.empty());

    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, k_serial_test_baudrate));
    REQUIRE(adapter.isOpen());

    const std::vector<std::uint8_t> payload{0x11U, 0x22U, 0x33U, 0x44U};
    REQUIRE_NOTHROW(adapter.send(payload));

    std::vector<std::uint8_t> readBuffer(payload.size(), 0U);
    const ssize_t readCount = ::read(pseudoTerminal.masterFd, readBuffer.data(), readBuffer.size());
    REQUIRE(readCount == static_cast<ssize_t>(payload.size()));
    CHECK(readBuffer == payload);

    adapter.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_serial_adapter_send_reports_write_failure", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    std::string setupError{};
    REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
    REQUIRE(setupError.empty());

    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, k_serial_test_baudrate));
    REQUIRE(adapter.isOpen());

    REQUIRE(pseudoTerminal.masterFd >= 0);
    ::close(pseudoTerminal.masterFd);
    pseudoTerminal.masterFd = -1;

    std::string errorMessage{};
    try {
        adapter.send(std::vector<std::uint8_t>{k_send_error_payload_byte_1, k_send_error_payload_byte_2});
    } catch (const yaha::YahaError& exceptionValue) {
        errorMessage = exceptionValue.buildMessage();
    }

    REQUIRE_FALSE(errorMessage.empty());
    REQUIRE(errorMessage.find("failed to write serial data") != std::string::npos);
    adapter.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_serial_adapter_receive_callback_gets_serial_bytes", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    std::string setupError{};
    REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
    REQUIRE(setupError.empty());

    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, k_serial_test_baudrate));

    std::mutex callbackMutex{};
    std::vector<std::uint8_t> callbackPayload{};
    adapter.setReceiveCallback([&callbackMutex, &callbackPayload](const std::vector<std::uint8_t>& payload) {
        std::lock_guard<std::mutex> lock{callbackMutex};
        callbackPayload = payload;
    });

    const std::vector<std::uint8_t> serialPayload{0xABU, 0xCDU, 0xEFU};
    const ssize_t writeCount = ::write(pseudoTerminal.masterFd, serialPayload.data(), serialPayload.size());
    REQUIRE(writeCount == static_cast<ssize_t>(serialPayload.size()));

    bool callbackReceived = false;
    for (int attemptIndex = 0; attemptIndex < k_callback_wait_attempts; ++attemptIndex) {
        {
            std::lock_guard<std::mutex> lock{callbackMutex};
            if (!callbackPayload.empty()) {
                callbackReceived = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_callback_wait_sleep_ms});
    }

    REQUIRE(callbackReceived);
    {
        std::lock_guard<std::mutex> lock{callbackMutex};
        CHECK(callbackPayload == serialPayload);
    }

    adapter.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_serial_adapter_open_supports_all_configured_baudrates", "[rs485_interface]") {
    std::vector<std::uint32_t> baudrates{
        k_baudrate_1200,
        k_baudrate_2400,
        k_baudrate_4800,
        k_baudrate_9600,
        k_baudrate_19200,
        k_baudrate_38400,
        k_baudrate_57600,
    };
#ifdef B115200
    baudrates.push_back(k_baudrate_115200);
#endif
#ifdef B230400
    baudrates.push_back(k_baudrate_230400);
#endif

    for (const std::uint32_t baudrate : baudrates) {
        PseudoTerminal pseudoTerminal{};
        std::string setupError{};
        REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
        REQUIRE(setupError.empty());

        yaha::Rs485SerialAdapter adapter{};
        REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, baudrate));
        REQUIRE(adapter.isOpen());
        adapter.close();
        REQUIRE_FALSE(adapter.isOpen());
    }
}

TEST_CASE("rs485_serial_adapter_receive_without_callback_is_ignored", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    std::string setupError{};
    REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
    REQUIRE(setupError.empty());

    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, k_serial_test_baudrate));
    REQUIRE(adapter.isOpen());

    const std::vector<std::uint8_t> serialPayload{0x10U, 0x20U, 0x30U};
    const ssize_t writeCount = ::write(pseudoTerminal.masterFd, serialPayload.data(), serialPayload.size());
    REQUIRE(writeCount == static_cast<ssize_t>(serialPayload.size()));

    std::this_thread::sleep_for(std::chrono::milliseconds{k_callback_wait_sleep_ms});
    adapter.close();
    REQUIRE_FALSE(adapter.isOpen());
}

TEST_CASE("rs485_serial_adapter_open_with_unknown_baudrate_uses_default_mapping", "[rs485_interface]") {
    PseudoTerminal pseudoTerminal{};
    std::string setupError{};
    REQUIRE(createPseudoTerminal(pseudoTerminal, setupError));
    REQUIRE(setupError.empty());

    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.open(pseudoTerminal.slavePath, 12345U));
    REQUIRE(adapter.isOpen());
    adapter.close();
    REQUIRE_FALSE(adapter.isOpen());
}

TEST_CASE("rs485_serial_adapter_close_is_idempotent", "[rs485_interface]") {
    yaha::Rs485SerialAdapter adapter{};
    REQUIRE_NOTHROW(adapter.close());
    REQUIRE_NOTHROW(adapter.close());
}
