#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_state/rs485_token_exchange.h"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint8_t k_my_address{5U};
constexpr std::uint8_t k_max_version{2U};
constexpr std::uint8_t k_high_sender_address{6U};
constexpr std::uint8_t k_lower_version{1U};
constexpr std::uint8_t k_custom_version{7U};
constexpr std::uint8_t k_mid_sender_address{8U};
constexpr std::uint32_t k_no_message_tick_budget{200U};

[[nodiscard]] yaha::Rs485SerialMessage makeEnableSendToken(
    const std::uint8_t sender,
    const std::uint8_t receiver,
    const std::uint8_t version) {
    yaha::Rs485SerialMessage message{};
    message.sender = sender;
    message.receiver = receiver;
    message.command = yaha::k_rs485_token_command;
    message.value = static_cast<double>(static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend));
    message.version = version;
    return message;
}

} // namespace

TEST_CASE("rs485_token_exchange_processes_only_token_command", "[rs485_state]") {
    yaha::Rs485TokenExchange exchange{k_my_address, k_max_version};

    yaha::Rs485SerialMessage message{};
    message.command = 'A';
    message.sender = 3U;
    message.receiver = k_my_address;

    const auto result = exchange.processStateMessage(message);

    REQUIRE(result.has_value() == false);
    REQUIRE(exchange.addressChain().empty());
}

TEST_CASE("rs485_token_exchange_null_coercion_min_side_effect_preserved", "[rs485_state]") {
    yaha::Rs485TokenExchange exchange{k_my_address, k_max_version};

    const auto token = makeEnableSendToken(
        k_high_sender_address,
        yaha::k_rs485_broadcast_address,
        k_max_version);

    (void)exchange.processStateMessage(token);

    REQUIRE(exchange.state().leftmostSibling().has_value());
    REQUIRE(*exchange.state().leftmostSibling() == 0U);
}

TEST_CASE("rs485_token_exchange_version_change_only_after_enable_send_gate", "[rs485_state]") {
    yaha::Rs485TokenExchange exchange{k_my_address, k_max_version};

    const auto incomingToken = makeEnableSendToken(
        k_high_sender_address,
        k_my_address,
        k_lower_version);

    (void)exchange.processStateMessage(incomingToken);
    REQUIRE(exchange.version() == k_max_version);

    yaha::Rs485SerialMessage sentEnableSend{};
    sentEnableSend.command = yaha::k_rs485_token_command;
    sentEnableSend.value = static_cast<double>(static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend));
    exchange.enableChangeVersion(sentEnableSend);

    (void)exchange.processStateMessage(incomingToken);
    REQUIRE(exchange.version() == k_lower_version);
}

TEST_CASE("rs485_token_exchange_setters_and_state_info_are_observable", "[rs485_state]") {
    yaha::Rs485TokenExchange exchange{k_my_address, k_max_version};

    exchange.setVersion(k_custom_version);
    REQUIRE(exchange.version() == k_custom_version);

    exchange.setMaySend(true);
    REQUIRE(exchange.maySend());

    const std::string stateInfo = exchange.getStateInfo();
    REQUIRE(stateInfo.find("State:") != std::string::npos);
    REQUIRE(stateInfo.find("Leftmost:") != std::string::npos);
    REQUIRE(stateInfo.find("Neighbour:") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_token_exchange_updates_leftmost_and_right_sibling_paths", "[rs485_state]") {
    yaha::Rs485TokenExchange exchange{k_my_address, k_max_version};

    const auto lowerSenderToken = makeEnableSendToken(3U, yaha::k_rs485_broadcast_address, k_max_version);
    const auto higherSenderToken = makeEnableSendToken(9U, yaha::k_rs485_broadcast_address, k_max_version);

    (void)exchange.processStateMessage(lowerSenderToken);
    (void)exchange.processStateMessage(higherSenderToken);
    (void)exchange.processStateMessage(makeEnableSendToken(k_mid_sender_address, yaha::k_rs485_broadcast_address, k_max_version));
    (void)exchange.processStateMessage(makeEnableSendToken(k_mid_sender_address, yaha::k_rs485_broadcast_address, k_max_version));

    REQUIRE(exchange.addressChain().size() == 3U);
    REQUIRE(exchange.addressChain()[0].address == 3U);
    REQUIRE(exchange.addressChain()[1].address == k_mid_sender_address);
    REQUIRE(exchange.addressChain()[2].address == 9U);

    bool sawEnableSend = false;
    for (std::uint32_t tick = 0U; tick < k_no_message_tick_budget; ++tick) {
        const auto maybeSignal = exchange.processStateNoMessage();
        if (!maybeSignal.has_value()) {
            continue;
        }
        const auto value = static_cast<std::uint8_t>(maybeSignal->value);
        if (value == static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend)) {
            sawEnableSend = true;
            REQUIRE(maybeSignal->receiver == k_mid_sender_address);
            break;
        }
    }

    REQUIRE(sawEnableSend);
}
