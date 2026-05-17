#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_state/rs485_scheduler.h"
#include "yaha/rs485_state/rs485_send_queue.h"

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint8_t k_my_address{5U};
constexpr std::uint8_t k_max_version{2U};
constexpr std::uint8_t k_receiver_address{8U};
constexpr std::uint32_t k_tick_delay_ms{100U};
constexpr std::uint32_t k_retry_limit{10U};
constexpr std::uint8_t k_token_sender_address{6U};
constexpr double k_value_1{1.0};
constexpr double k_value_2{2.0};
constexpr double k_value_5{5.0};
constexpr double k_value_7{7.0};
constexpr double k_value_9{9.0};

[[nodiscard]] yaha::Rs485SerialMessage makeQueuedMessage(
    const char command,
    const bool reply,
    const double value) {
    yaha::Rs485SerialMessage message{};
    message.sender = k_my_address;
    message.receiver = k_receiver_address;
    message.command = command;
    message.reply = reply;
    message.value = value;
    message.version = 1U;
    return message;
}

[[nodiscard]] yaha::Rs485SerialMessage makeResponseFor(const yaha::Rs485SerialMessage& queued) {
    yaha::Rs485SerialMessage response{};
    response.sender = queued.receiver;
    response.receiver = queued.sender;
    response.command = queued.command;
    response.value = queued.value;
    response.version = queued.version;
    return response;
}

[[nodiscard]] yaha::Rs485SerialMessage makeEnableSendTokenForMe() {
    yaha::Rs485SerialMessage message{};
    message.sender = k_token_sender_address;
    message.receiver = k_my_address;
    message.command = yaha::k_rs485_token_command;
    message.value = static_cast<double>(static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend));
    message.version = 1U;
    return message;
}

} // namespace

TEST_CASE("rs485_send_queue_replaces_same_sender_receiver_command_except_x", "[rs485_state]") {
    yaha::Rs485SendQueue queue{};

    const auto first = makeQueuedMessage('A', true, k_value_1);
    const auto second = makeQueuedMessage('A', true, k_value_2);

    queue.addMessage(first);
    queue.addMessage(second);

    REQUIRE(queue.hasMessages());
    const auto queued = queue.getMessage();
    REQUIRE(queued.has_value());
    REQUIRE(queued->value == k_value_2);
}

TEST_CASE("rs485_send_queue_command_x_never_replaced", "[rs485_state]") {
    yaha::Rs485SendQueue queue{};

    queue.addMessage(makeQueuedMessage('X', true, k_value_1));
    queue.addMessage(makeQueuedMessage('X', true, k_value_2));

    REQUIRE(queue.getMessage(0U).has_value());
    REQUIRE(queue.getMessage(1U).has_value());
}

TEST_CASE("rs485_scheduler_tick_order_state_then_queue_then_maysend_reset", "[rs485_state]") {
    yaha::Rs485Scheduler scheduler{k_my_address, k_max_version, k_tick_delay_ms};
    std::vector<yaha::Rs485SerialMessage> sent{};
    scheduler.setSendCallback([&sent](const yaha::Rs485SerialMessage& message) {
        sent.push_back(message);
    });

    (void)scheduler.processReceivedMessage(makeEnableSendTokenForMe());
    scheduler.sendMessage(makeQueuedMessage('B', false, k_value_9));

    scheduler.processTick();

    REQUIRE(sent.size() == 1U);
    REQUIRE(sent[0].command == 'B');
    REQUIRE(scheduler.tokenExchange().maySend() == false);
}

TEST_CASE("rs485_scheduler_response_match_dequeues_without_retry_counter_reset", "[rs485_state]") {
    yaha::Rs485Scheduler scheduler{k_my_address, k_max_version, k_tick_delay_ms};
    scheduler.setSendCallback([](const yaha::Rs485SerialMessage&) {
    });

    const auto queued = makeQueuedMessage('C', true, k_value_7);
    scheduler.sendMessage(queued);
    (void)scheduler.processReceivedMessage(makeEnableSendTokenForMe());
    scheduler.processTick();

    REQUIRE(scheduler.sendRetryCount() == 1U);
    REQUIRE(scheduler.queuedMessageCount() == 1U);

    const auto response = makeResponseFor(queued);
    (void)scheduler.processReceivedMessage(response);

    REQUIRE(scheduler.queuedMessageCount() == 0U);
    REQUIRE(scheduler.sendRetryCount() == 1U);
}

TEST_CASE("rs485_scheduler_retry_counter_resets_only_on_dequeue_in_send_loop", "[rs485_state]") {
    yaha::Rs485Scheduler scheduler{k_my_address, k_max_version, k_tick_delay_ms};
    scheduler.setSendCallback([](const yaha::Rs485SerialMessage&) {
    });

    scheduler.sendMessage(makeQueuedMessage('D', true, k_value_5));

    for (std::uint32_t index = 0U; index < k_retry_limit; ++index) {
        (void)scheduler.processReceivedMessage(makeEnableSendTokenForMe());
        scheduler.processTick();
    }

    REQUIRE(scheduler.queuedMessageCount() == 0U);
    REQUIRE(scheduler.sendRetryCount() == 0U);
}
