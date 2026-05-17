#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_state/rs485_state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t k_tick_iterations_until_loss{44U};

} // namespace

TEST_CASE("rs485_state_constructor_defaults_exact", "[rs485_state]") {
    yaha::Rs485State state{};

    REQUIRE(state.state() == yaha::Rs485StateId::Unknown);
    REQUIRE(state.timer() == 0U);
    REQUIRE(state.rightSibling().has_value() == false);
    REQUIRE(state.leftmostSibling().has_value() == false);
    REQUIRE(state.maySend() == false);
    REQUIRE(state.trace() == false);
}

TEST_CASE("rs485_state_unknown_registration_request_transitions_to_unregistered", "[rs485_state]") {
    yaha::Rs485State state{};

    const auto result = state.updateState(
        static_cast<std::uint8_t>(yaha::Rs485StateResult::RegistrationRequest),
        false);

    REQUIRE(result == yaha::Rs485StateResult::RegistrationInfo);
    REQUIRE(state.state() == yaha::Rs485StateId::Unregistered);
}

TEST_CASE("rs485_state_registered_token_lost_threshold_exact_40_ticks", "[rs485_state]") {
    yaha::Rs485State state{};
    const auto changed = state.updateState(
        static_cast<std::uint8_t>(yaha::Rs485StateResult::EnableSend),
        false);
    REQUIRE(changed == yaha::Rs485StateResult::StateChanged);
    REQUIRE(state.state() == yaha::Rs485StateId::Registered);

    yaha::Rs485StateResult lastResult = yaha::Rs485StateResult::Unchanged;
    for (std::uint32_t index = 0U; index < k_tick_iterations_until_loss; ++index) {
        lastResult = state.updateStateNoMessage();
    }

    REQUIRE(lastResult == yaha::Rs485StateResult::EnableSend);
    REQUIRE(state.tokenLost());
}

TEST_CASE("rs485_state_setstate_trace_logging_side_effect", "[rs485_state]") {
    yaha::Rs485State state{};
    std::vector<std::string> logs{};

    state.setTrace(true);
    state.setTraceLogCallback([&logs](const std::string& line) {
        logs.push_back(line);
    });

    const auto result = state.updateState(yaha::k_rs485_loop_timeout, false);

    REQUIRE(result == yaha::Rs485StateResult::StateChanged);
    REQUIRE(logs.size() == 1U);
    REQUIRE(logs[0].find("state changed to Reboot") != std::string::npos);
}
