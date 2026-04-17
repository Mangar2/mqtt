#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "data_model/session/session_state.h"
#include "store/session_store.h"
#include "store/store_error.h"

using namespace mqtt;
using namespace std::chrono_literals;

namespace {

SessionState make_session(const std::string &cid,
                          uint32_t expiry = 0U) {
    SessionState state;
    state.client_id.value = cid;
    state.session_expiry_interval = expiry;
    return state;
}

} // namespace

TEST_CASE("create_new_session", "[store]") {
    SessionStore store;
    store.create(make_session("c1"));
    CHECK(store.size() == 1U);
    CHECK(store.load("c1").has_value());
}

TEST_CASE("create_duplicate_throws", "[store]") {
    SessionStore store;
    store.create(make_session("c1"));
    CHECK_THROWS_AS(store.create(make_session("c1")), StoreException);
    try {
        store.create(make_session("c1"));
    } catch (const StoreException &exc) {
        CHECK(exc.error() == StoreError::SessionAlreadyExists);
    }
}

TEST_CASE("load_existing_session", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 30U));
    const auto result = store.load("c1");
    REQUIRE(result.has_value());
    CHECK(result->client_id.value == "c1");
    CHECK(result->session_expiry_interval == 30U);
}

TEST_CASE("load_unknown_returns_empty", "[store]") {
    SessionStore store;
    CHECK_FALSE(store.load("unknown").has_value());
}

TEST_CASE("remove_existing_session", "[store]") {
    SessionStore store;
    store.create(make_session("c1"));
    store.remove("c1");
    CHECK(store.size() == 0U);
    CHECK_FALSE(store.load("c1").has_value());
}

TEST_CASE("session_remove_unknown_is_noop", "[store]") {
    SessionStore store;
    store.create(make_session("c1"));
    store.remove("unknown");
    CHECK(store.size() == 1U);
}

TEST_CASE("contains_returns_true_when_present", "[store]") {
    SessionStore store;
    store.create(make_session("c1"));
    CHECK(store.contains("c1"));
}

TEST_CASE("contains_returns_false_when_absent", "[store]") {
    SessionStore store;
    CHECK_FALSE(store.contains("unknown"));
}

TEST_CASE("expired_sessions_immediate_expiry", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 0U));
    const auto now = std::chrono::steady_clock::now();
    store.mark_disconnected("c1", now - 1s);
    const auto expired = store.expired_sessions(now);
    REQUIRE(expired.size() == 1U);
    CHECK(expired.front().client_id.value == "c1");
}

TEST_CASE("expired_sessions_interval_elapsed", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 60U));
    const auto now = std::chrono::steady_clock::now();
    store.mark_disconnected("c1", now - 90s);
    const auto expired = store.expired_sessions(now);
    CHECK(expired.size() == 1U);
}

TEST_CASE("expired_sessions_interval_not_elapsed", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 60U));
    const auto now = std::chrono::steady_clock::now();
    store.mark_disconnected("c1", now - 30s);
    const auto expired = store.expired_sessions(now);
    CHECK(expired.empty());
}

TEST_CASE("expired_sessions_never_expires", "[store]") {
    SessionStore store;
    constexpr uint32_t k_never = 0xFFFF'FFFFU;
    store.create(make_session("c1", k_never));
    const auto now = std::chrono::steady_clock::now();
    store.mark_disconnected("c1", now - 9999s);
    const auto expired = store.expired_sessions(now);
    CHECK(expired.empty());
}

TEST_CASE("expired_sessions_no_disconnect_time", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 0U));
    const auto now = std::chrono::steady_clock::now();
    // No mark_disconnected call — session is still "connected"
    const auto expired = store.expired_sessions(now);
    CHECK(expired.empty());
}

TEST_CASE("remove_also_clears_disconnect_time", "[store]") {
    SessionStore store;
    store.create(make_session("c1", 0U));
    const auto now = std::chrono::steady_clock::now();
    store.mark_disconnected("c1", now - 1s);
    store.remove("c1");

    // Re-create same client — fresh session, no disconnect time
    store.create(make_session("c1", 0U));
    const auto expired = store.expired_sessions(now);
    CHECK(expired.empty());
}
