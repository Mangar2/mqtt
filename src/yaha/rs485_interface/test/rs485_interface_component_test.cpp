#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_interface/rs485_interface_component.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint8_t k_my_address{10U};
constexpr std::uint8_t k_device_address{5U};
constexpr std::uint8_t k_token_sender{11U};
constexpr std::uint32_t k_wait_timeout_ms{800U};
constexpr std::uint32_t k_wait_step_ms{10U};
constexpr std::uint32_t k_time_of_day_delay_seconds{3600U};
constexpr double k_value_on{1.0};
constexpr std::uint32_t k_value_on_raw{1U};

[[nodiscard]] yaha::Rs485InterfaceConfig makeComponentConfig() {
    yaha::Rs485InterfaceConfig config{};
    config.serialPortName = "/dev/null";
    config.myAddress = k_my_address;
    config.maxVersion = 1U;
    config.tickDelayMs = 2U;
    config.timeOfDayDelaySeconds = k_time_of_day_delay_seconds;
    config.subscribeQos = yaha::Qos::AtLeastOnce;
    config.traceLevel = "internal";
    config.blinkDelaySeconds = 0U;
    config.temporaryOnSeconds = 1U;

    config.interfaces["switch"] = yaha::Rs485InterfaceDefinition{
        .usedBy = {'P'},
        .map = {{"on", static_cast<std::uint16_t>(k_value_on_raw)}, {"off", 0U}}};
    config.settings['P'] = "/power";
    config.status['P'] = "/power";
    config.addresses["house/room/device"] = k_device_address;
    config.topics["house/room/device/switch/s1"] = yaha::Rs485TopicMapping{
        .command = 'X',
        .value = 2U,
        .address = k_device_address};
    return config;
}

[[nodiscard]] std::vector<std::uint8_t> encodeEnableSendForMe() {
    yaha::Rs485SerialMessage token{};
    token.sender = k_token_sender;
    token.receiver = k_my_address;
    token.command = yaha::k_rs485_token_command;
    token.value = static_cast<double>(static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend));
    token.version = 1U;
    token.reply = false;
    return yaha::encodeRs485SerialMessage(token);
}

[[nodiscard]] bool waitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{k_wait_timeout_ms};
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_wait_step_ms});
    }
    return predicate();
}

} // namespace

TEST_CASE("rs485_interface_component_derives_expected_subscriptions", "[rs485_interface]") {
    const yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    const auto subscriptions = component.getSubscriptions();

    REQUIRE(subscriptions.contains("+/+/+/power/set"));
    REQUIRE(subscriptions.contains("house/room/device/switch/s1/+") );
    REQUIRE(subscriptions.contains("$SYS/rs485Interface/#"));
    REQUIRE(subscriptions.contains("$MONITOR/rs485Interface/#"));
    REQUIRE(subscriptions.at("+/+/+/power/set") == yaha::Qos::AtLeastOnce);
}

TEST_CASE("rs485_interface_component_set_action_emits_serial_message_after_enable_send", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::mutex sentMutex{};
    std::vector<yaha::Rs485SerialMessage> sentMessages{};
    component.setSerialSendCallback([&sentMutex, &sentMessages](const std::vector<std::uint8_t>& bytes) {
           const yaha::Rs485SerialMessage message = yaha::decodeRs485SerialMessage(bytes, 0U);
        std::lock_guard<std::mutex> lock{sentMutex};
        sentMessages.push_back(message);
    });

    component.run();
    component.handleMessage(yaha::Message{"house/room/device/power/set", std::string{"on"}});

    const bool found = waitUntil([&component, &sentMutex, &sentMessages]() {
        component.feedSerialBytes(encodeEnableSendForMe());
        std::lock_guard<std::mutex> lock{sentMutex};
        return std::ranges::any_of(sentMessages, [](const yaha::Rs485SerialMessage& message) {
            return message.command == 'P' &&
                message.receiver == k_device_address &&
                message.reply &&
                message.value == k_value_on;
        });
    });

    component.close();
    REQUIRE(found);
}

TEST_CASE("rs485_interface_component_serial_input_publishes_mapped_mqtt_message", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
        return yaha::PublishResult::ok();
    });

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = k_value_on;
    serial.version = 1U;
    serial.reply = false;

    const auto bytes = yaha::encodeRs485SerialMessage(serial);
    component.feedSerialBytes(bytes);

    REQUIRE(published.size() == 1U);
    REQUIRE(published[0].topic() == "house/room/device/power");
    REQUIRE(std::get<std::string>(published[0].value()) == "on");
    REQUIRE(published[0].qos() == yaha::Qos::AtLeastOnce);
}

TEST_CASE("rs485_interface_component_accepts_trace_topics_in_sys_and_monitor_namespace", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$SYS/rs485Interface/trace/set", std::string{"internal"}}));
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$MONITOR/rs485Interface/trace/set", std::string{"messages"}}));

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
        return yaha::PublishResult::ok();
    });

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = k_value_on;
    serial.version = 1U;
    serial.reply = false;

    component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial));

    REQUIRE(published.size() == 1U);
    REQUIRE(published[0].topic() == "house/room/device/power");
}
