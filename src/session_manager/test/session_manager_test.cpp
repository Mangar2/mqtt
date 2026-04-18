#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "data_model/packet/connect_packet.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/session/session_state.h"
#include "session_manager/session_expiry_scheduler.h"
#include "session_manager/session_manager.h"
#include "session_manager/session_manager_error.h"
#include "session_manager/session_open_result.h"
#include "session_manager/session_takeover_handler.h"
#include "store/inflight_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// Helpers
//

namespace {

const std::chrono::steady_clock::time_point k_epoch{};

ConnectPacket make_connect(const std::string &cid, bool clean_start = true,
                           uint32_t expiry = 0U) {
  ConnectPacket pkt;
  pkt.client_id.value = cid;
  pkt.clean_start = clean_start;
  if (expiry != 0U) {
    Property prop;
    prop.id = PropertyId::SessionExpiryInterval;
    prop.value = expiry;
    pkt.properties.push_back(prop);
  }
  return pkt;
}

SessionState make_session(const std::string &cid, uint32_t expiry = 0U) {
  SessionState state;
  state.client_id.value = cid;
  state.session_expiry_interval = expiry;
  return state;
}

} // namespace

//
// SessionTakeoverHandler tests
//

TEST_CASE("takeover_handler_register_and_is_active", "[session_manager]") {
  SessionTakeoverHandler handler;
  handler.register_connection("c1", []() {});
  CHECK(handler.is_active("c1"));
  CHECK_FALSE(handler.is_active("c2"));
  CHECK(handler.size() == 1U);
}

TEST_CASE("takeover_handler_unregister_removes_entry", "[session_manager]") {
  SessionTakeoverHandler handler;
  handler.register_connection("c1", []() {});
  handler.unregister_connection("c1");
  CHECK_FALSE(handler.is_active("c1"));
  CHECK(handler.size() == 0U);
}

TEST_CASE("takeover_handler_unregister_noop_when_not_active",
          "[session_manager]") {
  SessionTakeoverHandler handler;
  CHECK_NOTHROW(handler.unregister_connection("unknown"));
}

TEST_CASE("takeover_handler_takeover_returns_false_when_not_active",
          "[session_manager]") {
  SessionTakeoverHandler handler;
  CHECK_FALSE(handler.takeover_if_exists("c1"));
}

TEST_CASE("takeover_handler_takeover_invokes_callback_and_returns_true",
          "[session_manager]") {
  SessionTakeoverHandler handler;
  bool closed = false;
  handler.register_connection("c1", [&closed]() { closed = true; });
  const bool result = handler.takeover_if_exists("c1");
  CHECK(result);
  CHECK(closed);
  CHECK_FALSE(handler.is_active("c1"));
}

TEST_CASE("takeover_handler_size_tracks_active_connections",
          "[session_manager]") {
  SessionTakeoverHandler handler;
  CHECK(handler.size() == 0U);
  handler.register_connection("a", []() {});
  handler.register_connection("b", []() {});
  CHECK(handler.size() == 2U);
  handler.unregister_connection("a");
  CHECK(handler.size() == 1U);
}

//
// SessionExpiryScheduler tests
//

TEST_CASE("expiry_scheduler_empty_returns_nothing", "[session_manager]") {
  SessionExpiryScheduler scheduler;
  CHECK(scheduler.collect_expired(k_epoch).empty());
}

TEST_CASE("expiry_scheduler_zero_interval_always_expired",
          "[session_manager]") {
  SessionExpiryScheduler scheduler;
  scheduler.schedule("c1", k_epoch, 0U);
  const auto expired = scheduler.collect_expired(k_epoch);
  REQUIRE(expired.size() == 1U);
  CHECK(expired.front() == "c1");
}

TEST_CASE("expiry_scheduler_normal_interval_expired_after_deadline",
          "[session_manager]") {
  SessionExpiryScheduler scheduler;
  scheduler.schedule("c1", k_epoch, 60U);
  // At deadline — should be expired.
  const auto at_deadline = k_epoch + 60s;
  const auto expired = scheduler.collect_expired(at_deadline);
  REQUIRE(expired.size() == 1U);
  CHECK(expired.front() == "c1");
}

TEST_CASE("expiry_scheduler_normal_interval_not_expired_before_deadline",
          "[session_manager]") {
  SessionExpiryScheduler scheduler;
  scheduler.schedule("c1", k_epoch, 60U);
  const auto before_deadline = k_epoch + 59s;
  CHECK(scheduler.collect_expired(before_deadline).empty());
}

TEST_CASE("expiry_scheduler_never_expires_not_returned", "[session_manager]") {
  SessionExpiryScheduler scheduler;
  scheduler.schedule("c1", k_epoch, 0xFFFF'FFFFU);
  // Even far in the future — should never be returned.
  const auto far_future = k_epoch + std::chrono::hours(24 * 365 * 100);
  CHECK(scheduler.collect_expired(far_future).empty());
}

TEST_CASE("expiry_scheduler_cancel_removes_entry", "[session_manager]") {
  SessionExpiryScheduler scheduler;
  scheduler.schedule("c1", k_epoch, 0U);
  scheduler.cancel("c1");
  CHECK(scheduler.collect_expired(k_epoch).empty());
  CHECK(scheduler.size() == 0U);
}

TEST_CASE("expiry_scheduler_size_tracks_timers", "[session_manager]") {
  SessionExpiryScheduler scheduler;
  CHECK(scheduler.size() == 0U);
  scheduler.schedule("a", k_epoch, 30U);
  scheduler.schedule("b", k_epoch, 60U);
  CHECK(scheduler.size() == 2U);
  scheduler.cancel("a");
  CHECK(scheduler.size() == 1U);
}

//
// SessionManager tests
//

namespace {

struct SessionManagerFixture {
  SessionStore session_store;
  SubscriptionStore subscription_store;
  InflightStore inflight_store;
  SessionTakeoverHandler takeover_handler;
  SessionExpiryScheduler expiry_scheduler;
  SessionManager manager{session_store, subscription_store, inflight_store,
                         takeover_handler, expiry_scheduler};
};

} // namespace

TEST_CASE("session_manager_empty_client_id_throws", "[session_manager]") {
  SessionManagerFixture fix;
  ConnectPacket pkt;
  pkt.client_id.value = "";
  pkt.clean_start = true;
  CHECK_THROWS_AS(fix.manager.handle_connect(pkt, []() {}),
                  SessionManagerException);
  try {
    fix.manager.handle_connect(pkt, []() {});
  } catch (const SessionManagerException &exc) {
    CHECK(exc.error() == SessionManagerError::InvalidClientId);
  }
}

TEST_CASE("session_manager_clean_start_creates_new_session",
          "[session_manager]") {
  SessionManagerFixture fix;
  const auto result =
      fix.manager.handle_connect(make_connect("c1", true), []() {});
  CHECK_FALSE(result.session_present);
  CHECK_FALSE(result.takeover_occurred);
  CHECK(fix.session_store.contains("c1"));
}

TEST_CASE("session_manager_clean_start_removes_existing_session",
          "[session_manager]") {
  SessionManagerFixture fix;
  fix.session_store.create(make_session("c1", 300U));

  const auto result =
      fix.manager.handle_connect(make_connect("c1", true), []() {});
  CHECK_FALSE(result.session_present);
  // A fresh session has been created (expiry from CONNECT = 0 since no property
  // set).
  REQUIRE(fix.session_store.contains("c1"));
  CHECK(fix.session_store.load("c1")->session_expiry_interval == 0U);
}

TEST_CASE("session_manager_resume_no_existing_session", "[session_manager]") {
  SessionManagerFixture fix;
  const auto result =
      fix.manager.handle_connect(make_connect("c1", false), []() {});
  CHECK_FALSE(result.session_present);
  CHECK(fix.session_store.contains("c1"));
}

TEST_CASE("session_manager_resume_existing_session", "[session_manager]") {
  SessionManagerFixture fix;
  fix.session_store.create(make_session("c1", 300U));

  const auto result =
      fix.manager.handle_connect(make_connect("c1", false), []() {});
  CHECK(result.session_present);
  CHECK_FALSE(result.takeover_occurred);
}

TEST_CASE("session_manager_resume_cancels_expiry_timer", "[session_manager]") {
  SessionManagerFixture fix;
  fix.session_store.create(make_session("c1", 300U));
  fix.expiry_scheduler.schedule("c1", k_epoch, 300U);

  fix.manager.handle_connect(make_connect("c1", false), []() {});
  // Timer should have been cancelled.
  CHECK(fix.expiry_scheduler.collect_expired(k_epoch + 300s).empty());
}

TEST_CASE("session_manager_takeover_invokes_close_callback",
          "[session_manager]") {
  SessionManagerFixture fix;
  bool old_closed = false;
  fix.manager.handle_connect(make_connect("c1", true),
                             [&old_closed]() { old_closed = true; });

  // Second connection with same client_id triggers takeover.
  const auto result =
      fix.manager.handle_connect(make_connect("c1", true), []() {});
  CHECK(old_closed);
  CHECK(result.takeover_occurred);
}

TEST_CASE("session_manager_disconnect_zero_expiry_removes_session",
          "[session_manager]") {
  SessionManagerFixture fix;
  fix.manager.handle_connect(make_connect("c1", true, 0U), []() {});
  fix.manager.handle_disconnect("c1", std::nullopt, k_epoch);
  CHECK_FALSE(fix.session_store.contains("c1"));
}

TEST_CASE("session_manager_disconnect_nonzero_expiry_schedules_timer",
          "[session_manager]") {
  SessionManagerFixture fix;
  fix.manager.handle_connect(make_connect("c1", true, 60U), []() {});
  fix.manager.handle_disconnect("c1", std::nullopt, k_epoch);
  // Session should still exist.
  CHECK(fix.session_store.contains("c1"));
  // Expiry timer should be scheduled.
  const auto expired = fix.expiry_scheduler.collect_expired(k_epoch + 60s);
  REQUIRE(expired.size() == 1U);
  CHECK(expired.front() == "c1");
}

TEST_CASE("session_manager_disconnect_expiry_override", "[session_manager]") {
  SessionManagerFixture fix;
  // Session stored with expiry=60, but DISCONNECT overrides to 0 (immediate
  // drop).
  fix.manager.handle_connect(make_connect("c1", true, 60U), []() {});
  fix.manager.handle_disconnect("c1", std::optional<uint32_t>{0U}, k_epoch);
  CHECK_FALSE(fix.session_store.contains("c1"));
}

TEST_CASE("session_manager_cleanup_expired_removes_sessions",
          "[session_manager]") {
  SessionManagerFixture fix;
  // Create two sessions: one with expiry=30, one with expiry=never.
  fix.manager.handle_connect(make_connect("c1", true, 30U), []() {});
  fix.manager.handle_connect(make_connect("c2", true, 0xFFFF'FFFFU), []() {});
  fix.manager.handle_disconnect("c1", std::nullopt, k_epoch);
  fix.manager.handle_disconnect("c2", std::nullopt, k_epoch);

  const auto cleaned = fix.manager.cleanup_expired(k_epoch + 30s);
  REQUIRE(cleaned.size() == 1U);
  CHECK(cleaned.front() == "c1");
  CHECK_FALSE(fix.session_store.contains("c1"));
  // c2 must still exist.
  CHECK(fix.session_store.contains("c2"));
}

TEST_CASE("session_manager_cleanup_expired_cancels_timer",
          "[session_manager]") {
  SessionManagerFixture fix;
  fix.manager.handle_connect(make_connect("c1", true, 10U), []() {});
  fix.manager.handle_disconnect("c1", std::nullopt, k_epoch);
  fix.manager.cleanup_expired(k_epoch + 10s);
  // After cleanup no lingering timer entry.
  CHECK(fix.expiry_scheduler.size() == 0U);
}

TEST_CASE("session_manager_inflight_store_accessor_returns_shared_store",
          "[session_manager]") {
  SessionManagerFixture fix;
  CHECK(&fix.manager.inflight_store() == &fix.inflight_store);
}
