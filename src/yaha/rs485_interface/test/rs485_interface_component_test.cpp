#include <catch2/catch_test_macros.hpp>

#include "yaha/message/test/message_log_test_support.h"
#include "yaha/rs485_interface/rs485_interface_component.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
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

TEST_CASE("rs485_interface_component_matches_set_reason_chain_on_reply", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
        return yaha::PublishResult::ok();
    });

    yaha::Message action{"house/room/device/power/set", std::string{"on"}};
    action.addReason("request by test");
    component.handleMessage(action);

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = k_value_on;
    serial.version = 1U;
    serial.reply = false;
    component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial));

    REQUIRE(published.size() == 1U);
    REQUIRE(published[0].reason().size() == 3U);
    CHECK(published[0].reason()[0].message == "received by RS485Interface service");
    CHECK(published[0].reason()[1].message == "request by test");
    CHECK(published[0].reason()[2].message == "received from arduino");
}

TEST_CASE("rs485_interface_component_consume_match_entry_on_topic_match_even_for_value_mismatch", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
        return yaha::PublishResult::ok();
    });

    yaha::Message action{"house/room/device/power/set", std::string{"on"}};
    action.addReason("request by test");
    component.handleMessage(action);

    yaha::Rs485SerialMessage firstReply{};
    firstReply.sender = k_device_address;
    firstReply.receiver = k_my_address;
    firstReply.command = 'P';
    firstReply.value = 0.0;
    firstReply.version = 1U;
    firstReply.reply = false;
    component.feedSerialBytes(yaha::encodeRs485SerialMessage(firstReply));

    yaha::Rs485SerialMessage secondReply{};
    secondReply.sender = k_device_address;
    secondReply.receiver = k_my_address;
    secondReply.command = 'P';
    secondReply.value = k_value_on;
    secondReply.version = 1U;
    secondReply.reply = false;
    component.feedSerialBytes(yaha::encodeRs485SerialMessage(secondReply));

    REQUIRE(published.size() == 2U);
    REQUIRE(published[0].reason().size() == 1U);
    REQUIRE(published[1].reason().size() == 1U);
    CHECK(published[0].reason()[0].message == "received from arduino");
    CHECK(published[1].reason()[0].message == "received from arduino");
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

TEST_CASE("rs485_interface_component_trace_messages_logs_non_internal_with_legacy_format", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.traceLevel = "messages";

    yaha::Rs485InterfaceComponent component{config};

    std::ostringstream captured{};
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    REQUIRE_NOTHROW(component.feedSerialBytes(encodeEnableSendForMe()));

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = 1.0;
    serial.version = 1U;
    serial.reply = false;
    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial)));

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find("! = enable send") == std::string::npos);
    REQUIRE(output.find("5 => 10 (r:0): P = 1") != std::string::npos);
    REQUIRE(output.find("([9]  05 0a 02 09 50 00 01") != std::string::npos);
}

TEST_CASE("rs485_interface_component_trace_internal_logs_token_and_non_token", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.traceLevel = "internal";

    yaha::Rs485InterfaceComponent component{config};

    std::ostringstream captured{};
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    REQUIRE_NOTHROW(component.feedSerialBytes(encodeEnableSendForMe()));

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = 1.0;
    serial.version = 1U;
    serial.reply = false;
    REQUIRE_NOTHROW(component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial)));

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find("11 => 10 (r:0): ! = enable send") != std::string::npos);
    REQUIRE(output.find("([9]  0b 0a 02 09 21 00 01") != std::string::npos);
    REQUIRE(output.find("5 => 10 (r:0): P = 1") != std::string::npos);
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

TEST_CASE("rs485_interface_component_logs_incoming_message_when_enabled", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.logIncomingMessages = true;

    yaha::Rs485InterfaceComponent component{config};

    std::ostringstream captured{};
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    component.handleMessage(yaha::Message{"house/room/device/power/set", std::string{"on"}});

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find(yaha::test::messageLogLinePrefix(
        "rs485_interface", yaha::MessageLogDirection::Incoming, "house/room/device/power/set"))
        != std::string::npos);
}

TEST_CASE("rs485_interface_component_logs_outgoing_message_when_enabled", "[rs485_interface]") {
    yaha::Rs485InterfaceConfig config = makeComponentConfig();
    config.logOutgoingMessages = true;

    yaha::Rs485InterfaceComponent component{config};
    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });

    yaha::Rs485SerialMessage serial{};
    serial.sender = k_device_address;
    serial.receiver = k_my_address;
    serial.command = 'P';
    serial.value = k_value_on;
    serial.version = 1U;
    serial.reply = false;

    std::ostringstream captured{};
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    component.feedSerialBytes(yaha::encodeRs485SerialMessage(serial));

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find(yaha::test::messageLogLinePrefix(
        "rs485_interface", yaha::MessageLogDirection::Outgoing, "house/room/device/power"))
        != std::string::npos);
}

TEST_CASE("rs485_interface_component_does_not_log_when_disabled", "[rs485_interface]") {
    yaha::Rs485InterfaceComponent component{makeComponentConfig()};

    std::ostringstream captured{};
    std::streambuf* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    component.handleMessage(yaha::Message{"house/room/device/power/set", std::string{"on"}});

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find("rs485_interface") == std::string::npos);
}
