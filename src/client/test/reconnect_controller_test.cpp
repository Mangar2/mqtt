#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>

#include "client/reconnect_controller.h"

namespace mqtt {

namespace {

[[nodiscard]] ConnectionNegotiationResult make_negotiation_result() {
  ConnectionNegotiationResult negotiation_result;
  negotiation_result.session_present = true;
  negotiation_result.reason_code = ReasonCode::Success;
  negotiation_result.receive_maximum = 10U;
  negotiation_result.topic_alias_maximum = 4U;
  return negotiation_result;
}

} // namespace

TEST_CASE("reconnect_controller_transport_disconnect_attempts_and_recovers",
          "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{.initial_delay =
                                                            std::chrono::milliseconds{0},
                                                        .max_delay =
                                                            std::chrono::milliseconds{1000},
                                                        .multiplier = 2.0});

  std::size_t negotiate_calls = 0U;
  controller.set_negotiate_callback([&negotiate_calls]() {
    ++negotiate_calls;
    return make_negotiation_result();
  });

  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::TransportError, start_time);

  const ReconnectTickResult tick_result = controller.tick(start_time);
  CHECK(tick_result.attempted);
  CHECK(tick_result.reconnected);
  CHECK(controller.state() == ReconnectState::Connected);
  CHECK(negotiate_calls == 1U);
}

TEST_CASE("reconnect_controller_failure_backoff_progression_and_cap",
          "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{.initial_delay =
                                                            std::chrono::milliseconds{100},
                                                        .max_delay =
                                                            std::chrono::milliseconds{250},
                                                        .multiplier = 2.0});

  controller.set_negotiate_callback(
      []() -> ConnectionNegotiationResult { throw std::runtime_error("down"); });

  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::TransportError, start_time);

  const ReconnectTickResult first_result =
      controller.tick(start_time + std::chrono::milliseconds{100});
  CHECK(first_result.attempted);
  CHECK_FALSE(first_result.reconnected);
  CHECK(controller.current_delay() == std::chrono::milliseconds{200});

  const ReconnectTickResult second_result =
      controller.tick(start_time + std::chrono::milliseconds{300});
  CHECK(second_result.attempted);
  CHECK_FALSE(second_result.reconnected);
  CHECK(controller.current_delay() == std::chrono::milliseconds{250});

  const ReconnectTickResult third_result =
      controller.tick(start_time + std::chrono::milliseconds{550});
  CHECK(third_result.attempted);
  CHECK_FALSE(third_result.reconnected);
  CHECK(controller.current_delay() == std::chrono::milliseconds{250});
}

TEST_CASE(
    "reconnect_controller_keepalive_timeout_trigger_behaves_like_transport_drop",
    "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{.initial_delay =
                                                            std::chrono::milliseconds{10},
                                                        .max_delay =
                                                            std::chrono::milliseconds{1000},
                                                        .multiplier = 2.0});

  controller.set_negotiate_callback([] { return make_negotiation_result(); });
  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::KeepAliveTimeout, start_time);

  CHECK(controller.state() == ReconnectState::WaitingForRetry);
  const ReconnectTickResult tick_result =
      controller.tick(start_time + std::chrono::milliseconds{10});
  CHECK(tick_result.attempted);
  CHECK(tick_result.reconnected);
}

TEST_CASE("reconnect_controller_user_disconnect_disables_auto_reconnect",
          "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{});
  std::size_t negotiate_calls = 0U;
  controller.set_negotiate_callback([&negotiate_calls]() {
    ++negotiate_calls;
    return make_negotiation_result();
  });

  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::UserInitiated, start_time);
  CHECK(controller.state() == ReconnectState::Disabled);

  const ReconnectTickResult tick_result =
      controller.tick(start_time + std::chrono::seconds{5});
  CHECK_FALSE(tick_result.attempted);
  CHECK_FALSE(tick_result.reconnected);
  CHECK(negotiate_calls == 0U);
}

TEST_CASE(
    "reconnect_controller_success_invokes_restore_callbacks_and_resets_counters",
    "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{.initial_delay =
                                                            std::chrono::milliseconds{0},
                                                        .max_delay =
                                                            std::chrono::milliseconds{1000},
                                                        .multiplier = 2.0});

  bool restore_session_called = false;
  bool restore_qos_called = false;

  controller.set_negotiate_callback([] { return make_negotiation_result(); });
  controller.set_restore_session_callback(
      [&restore_session_called](const ConnectionNegotiationResult &result) {
        CHECK(result.session_present);
        restore_session_called = true;
      });
  controller.set_restore_qos_callback(
      [&restore_qos_called]() { restore_qos_called = true; });

  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::TransportError, start_time);
  const ReconnectTickResult tick_result = controller.tick(start_time);

  CHECK(tick_result.reconnected);
  CHECK(restore_session_called);
  CHECK(restore_qos_called);
  CHECK(controller.failed_attempts() == 0U);
  CHECK(controller.current_delay() == std::chrono::milliseconds{0});
}

TEST_CASE("reconnect_controller_missing_negotiate_callback_records_error",
          "[client][reconnect]") {
  ReconnectController controller(ReconnectBackoffPolicy{.initial_delay =
                                                            std::chrono::milliseconds{50},
                                                        .max_delay =
                                                            std::chrono::milliseconds{100},
                                                        .multiplier = 2.0});

  const auto start_time = ReconnectController::Clock::now();
  controller.on_connection_lost(ReconnectTrigger::TransportError, start_time);

  const ReconnectTickResult tick_result =
      controller.tick(start_time + std::chrono::milliseconds{50});
  CHECK(tick_result.attempted);
  CHECK_FALSE(tick_result.reconnected);
  REQUIRE(tick_result.error_message.has_value());
  CHECK_FALSE(tick_result.error_message->empty());
  CHECK(controller.failed_attempts() == 1U);
  CHECK(controller.next_retry_at().has_value());
}

} // namespace mqtt
