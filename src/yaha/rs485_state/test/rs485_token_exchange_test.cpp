#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_state/rs485_token_exchange.h"

#include <cstdint>

namespace {

constexpr std::uint8_t k_my_address{5U};
constexpr std::uint8_t k_max_version{2U};
constexpr std::uint8_t k_high_sender_address{6U};
constexpr std::uint8_t k_lower_version{1U};

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
