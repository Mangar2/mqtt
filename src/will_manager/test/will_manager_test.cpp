#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/qos.h"
#include "will_manager/will_delay_timer.h"
#include "will_manager/will_publisher.h"
#include "will_manager/will_store.h"

using namespace mqtt;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

const std::chrono::steady_clock::time_point k_epoch{};

WillMessage make_will(const std::string &topic, uint32_t delay = 0U) {
  WillMessage will_msg;
  will_msg.message.topic.value = topic;
  will_msg.message.qos = QoS::AtMostOnce;
  will_msg.delay_interval = delay;
  return will_msg;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// WillStore tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("will_store_store_and_load", "[will_manager]") {
  WillStore will_store;
  const WillMessage will = make_will("sensor/temp");
  will_store.store("c1", will);
  const auto loaded = will_store.load("c1");
  REQUIRE(loaded.has_value());
  CHECK(loaded->message.topic.value == "sensor/temp");
}

TEST_CASE("will_store_load_absent", "[will_manager]") {
  WillStore will_store;
  CHECK_FALSE(will_store.load("missing").has_value());
}

TEST_CASE("will_store_remove_exists", "[will_manager]") {
  WillStore will_store;
  will_store.store("c1", make_will("t1"));
  will_store.remove("c1");
  CHECK_FALSE(will_store.load("c1").has_value());
}

TEST_CASE("will_store_remove_noop", "[will_manager]") {
  WillStore will_store;
  CHECK_NOTHROW(will_store.remove("missing"));
  CHECK(will_store.size() == 0U);
}

TEST_CASE("will_store_overwrite", "[will_manager]") {
  WillStore will_store;
  will_store.store("c1", make_will("old/topic"));
  will_store.store("c1", make_will("new/topic"));
  const auto loaded = will_store.load("c1");
  REQUIRE(loaded.has_value());
  CHECK(loaded->message.topic.value == "new/topic");
}

TEST_CASE("will_store_size", "[will_manager]") {
  WillStore will_store;
  will_store.store("c1", make_will("t1"));
  will_store.store("c2", make_will("t2"));
  CHECK(will_store.size() == 2U);
  will_store.remove("c1");
  CHECK(will_store.size() == 1U);
}

// ─────────────────────────────────────────────────────────────────────────────
// WillDelayTimer tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("will_delay_timer_schedule_and_collect", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 5U);
  const auto due = timer.collect_due(k_epoch + 5s);
  REQUIRE(due.size() == 1U);
  CHECK(due[0] == "c1");
}

TEST_CASE("will_delay_timer_not_yet_due", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 5U);
  CHECK(timer.collect_due(k_epoch + 4s).empty());
}

TEST_CASE("will_delay_timer_zero_delay", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 0U);
  const auto due = timer.collect_due(k_epoch);
  REQUIRE(due.size() == 1U);
  CHECK(due[0] == "c1");
}

TEST_CASE("will_delay_timer_cancel", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 0U);
  timer.cancel("c1");
  CHECK(timer.collect_due(k_epoch).empty());
}

TEST_CASE("will_delay_timer_cancel_noop", "[will_manager]") {
  WillDelayTimer timer;
  CHECK_NOTHROW(timer.cancel("missing"));
}

TEST_CASE("will_delay_timer_overwrite", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 10U);
  timer.schedule("c1", k_epoch, 2U);
  CHECK(timer.collect_due(k_epoch + 2s).size() == 1U);
  CHECK(timer.collect_due(k_epoch + 1s).empty());
}

TEST_CASE("will_delay_timer_size", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 5U);
  timer.schedule("c2", k_epoch, 5U);
  timer.schedule("c3", k_epoch, 5U);
  CHECK(timer.size() == 3U);
  timer.cancel("c2");
  CHECK(timer.size() == 2U);
}

TEST_CASE("will_delay_timer_collect_does_not_remove", "[will_manager]") {
  WillDelayTimer timer;
  timer.schedule("c1", k_epoch, 0U);
  CHECK(timer.collect_due(k_epoch).size() == 1U);
  CHECK(timer.collect_due(k_epoch).size() == 1U);
}

// ─────────────────────────────────────────────────────────────────────────────
// WillPublisher tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("will_publisher_on_connect_stores_will", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status"));
  CHECK(will_store.load("c1").has_value());
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_on_reconnect_cancels_timer", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 0U));
  wdt.schedule("c1", k_epoch,
               0U); // manually arm timer to simulate prior connection loss
  pub.on_reconnect("c1");
  pub.publish_due(k_epoch);
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_on_disconnect_normal_suppresses", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 5U));
  pub.on_disconnect("c1", ReasonCode::Success, k_epoch);
  pub.publish_due(k_epoch + 5s);
  CHECK(calls == 0);
  CHECK_FALSE(will_store.load("c1").has_value());
}

TEST_CASE("will_publisher_on_disconnect_with_will_arms_timer",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 5U));
  pub.on_disconnect("c1", ReasonCode::DisconnectWithWill, k_epoch);
  pub.publish_due(k_epoch + 4s);
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_on_disconnect_with_will_publishes_after_delay",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 5U));
  pub.on_disconnect("c1", ReasonCode::DisconnectWithWill, k_epoch);
  pub.publish_due(k_epoch + 5s);
  CHECK(calls == 1);
}

TEST_CASE("will_publisher_on_disconnect_zero_delay_publishes_immediately",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 0U));
  pub.on_disconnect("c1", ReasonCode::DisconnectWithWill, k_epoch);
  CHECK(calls == 1);
}

TEST_CASE("will_publisher_on_connection_lost_arms_timer", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 2U));
  pub.on_connection_lost("c1", k_epoch);
  pub.publish_due(k_epoch + 1s);
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_on_connection_lost_publishes_after_delay",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 2U));
  pub.on_connection_lost("c1", k_epoch);
  pub.publish_due(k_epoch + 2s);
  CHECK(calls == 1);
}

TEST_CASE("will_publisher_on_connection_lost_zero_delay_publishes",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 0U));
  pub.on_connection_lost("c1", k_epoch);
  CHECK(calls == 1);
}

TEST_CASE("will_publisher_on_session_expired_publishes_pending_will",
          "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 10U));
  pub.on_connection_lost("c1", k_epoch);
  pub.on_session_expired("c1");
  CHECK(calls == 1);
  CHECK_FALSE(will_store.load("c1").has_value());
  CHECK(wdt.size() == 0U);
}

TEST_CASE("will_publisher_on_session_expired_no_will_noop", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  CHECK_NOTHROW(pub.on_session_expired("c1"));
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_publish_due_removes_state", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  pub.on_connect("c1", make_will("dev/status", 0U));
  pub.on_connection_lost("c1", k_epoch);
  // Will was already published inline (delay==0); a second publish_due is
  // no-op.
  pub.publish_due(k_epoch);
  CHECK(calls == 1);
}

TEST_CASE("will_publisher_on_disconnect_no_will_noop", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  CHECK_NOTHROW(
      pub.on_disconnect("c1", ReasonCode::DisconnectWithWill, k_epoch));
  CHECK(calls == 0);
}

TEST_CASE("will_publisher_on_connection_lost_no_will_noop", "[will_manager]") {
  WillStore will_store;
  WillDelayTimer wdt;
  int calls = 0;
  WillPublisher pub{will_store, wdt, [&](const WillMessage &) { ++calls; }};

  CHECK_NOTHROW(pub.on_connection_lost("c1", k_epoch));
  CHECK(calls == 0);
}
