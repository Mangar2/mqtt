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
constexpr double k_blink_cycles_numeric{2.0};
constexpr double k_non_integer_blink_value{1.25};
constexpr double k_trace_numeric_payload_one{1.0};
constexpr double k_trace_numeric_payload_two{2.0};
constexpr std::uint8_t k_unknown_sender_address{99U};
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

TEST_CASE("rs485_interface_component_ignores_unknown_action_suffix", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::mutex sentMutex{};
    std::vector<yaha::Rs485SerialMessage> sentMessages{};
    component.setSerialSendCallback([&sentMutex, &sentMessages](const std::vector<std::uint8_t>& bytes) {
        const yaha::Rs485SerialMessage message = yaha::decodeRs485SerialMessage(bytes, 0U);
        std::lock_guard<std::mutex> lock{sentMutex};
        sentMessages.push_back(message);
    });

    component.run();
    component.handleMessage(yaha::Message{"house/room/device/power/invalid", std::string{"on"}});

    const bool hasNoSends = waitUntil([&component, &sentMutex, &sentMessages]() {
        component.feedSerialBytes(encodeEnableSendForMe());
        std::lock_guard<std::mutex> lock{sentMutex};
        return sentMessages.empty();
    });

    component.close();
    REQUIRE(hasNoSends);
}

TEST_CASE("rs485_interface_component_trace_topics_accept_non_string_payload_without_change", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.traceLevel = "error";
    yaha::Rs485InterfaceComponent component{config};

    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$SYS/rs485Interface/trace/set", k_trace_numeric_payload_one}));
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$MONITOR/rs485Interface/trace/set", k_trace_numeric_payload_two}));
}

TEST_CASE("rs485_interface_component_run_and_close_are_idempotent", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    REQUIRE_NOTHROW(component.close());
    REQUIRE_NOTHROW(component.run());
    REQUIRE_NOTHROW(component.run());
    REQUIRE_NOTHROW(component.close());
    REQUIRE_NOTHROW(component.close());
}

TEST_CASE("rs485_interface_component_decode_and_map_errors_are_handled", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.traceLevel = "error";

    yaha::Rs485InterfaceComponent component{config};

    const std::vector<std::uint8_t> invalidFrame{255U, 1U, 2U};
    REQUIRE_NOTHROW(component.feedSerialBytes(invalidFrame));

    yaha::Rs485SerialMessage unknownMapping{};
    unknownMapping.sender = k_unknown_sender_address;
    unknownMapping.receiver = k_my_address;
    unknownMapping.command = 'P';
    unknownMapping.value = 1.0;
    unknownMapping.version = 1U;
    unknownMapping.reply = false;
    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(unknownMapping)));
}

TEST_CASE("rs485_interface_component_log_flags_cover_incoming_and_outgoing_paths", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.logIncomingMessages = true;
    config.logOutgoingMessages = true;

    yaha::Rs485InterfaceComponent component{config};
    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });
    component.setSerialSendCallback([](const std::vector<std::uint8_t>&) {
    });

    component.run();
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/room/device/power/set", 1.0}));
    REQUIRE_NOTHROW(component.feedSerialBytes(encodeEnableSendForMe()));

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = 1.0;
    serial.version = 1U;
    serial.reply = false;
    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial)));
    REQUIRE_NOTHROW(component.close());
}

TEST_CASE("rs485_interface_component_get_subscriptions_covers_join_topic_variants", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.settings['P'] = "power";
    config.settings['X'] = "";

    yaha::Rs485InterfaceComponent component{config};
    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();

    CHECK(subscriptions.contains("+/+/+/power/set"));
    CHECK(subscriptions.contains("+/+/+//set"));
}

TEST_CASE("rs485_interface_component_inbound_publish_without_callback_is_ignored", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    yaha::Rs485InterfaceComponent component{config};

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = 1.0;
    serial.version = 1U;
    serial.reply = false;

    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial)));
}

TEST_CASE("rs485_interface_component_blink_actions_when_stopped_do_not_throw", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.blinkDelaySeconds = 0U;

    yaha::Rs485InterfaceComponent component{config};
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/room/device/power/blink", std::string{"2"}}));
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/room/device/power/blink", k_non_integer_blink_value}));
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/room/device/power/blink", std::string{"0"}}));
    REQUIRE_NOTHROW(component.close());
}

TEST_CASE("rs485_interface_component_numeric_state_cache_and_cached_blink_path", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.interfaces.clear();
    config.blinkDelaySeconds = 0U;

    yaha::Rs485InterfaceComponent component{config};

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = 1.0;
    serial.version = 1U;
    serial.reply = false;
    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial)));

    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/room/device/power/blink", k_blink_cycles_numeric}));
    REQUIRE_NOTHROW(component.close());
}
