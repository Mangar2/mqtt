#include <catch2/catch_test_macros.hpp>

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/session/session_state.h"
#include "data_model/subscription/subscription.h"

using namespace mqtt;

// ── InflightDirection (1.7.2) ─────────────────────────────────────────────────

TEST_CASE("inflight_direction_values", "[session]")
{
    CHECK(static_cast<uint8_t>(InflightDirection::Inbound)  == 0U);
    CHECK(static_cast<uint8_t>(InflightDirection::Outbound) == 1U);
}

// ── InflightState (1.7.2) ─────────────────────────────────────────────────────

TEST_CASE("inflight_state_values", "[session]")
{
    CHECK(static_cast<uint8_t>(InflightState::WaitingForPuback)  == 0U);
    CHECK(static_cast<uint8_t>(InflightState::WaitingForPubrec)  == 1U);
    CHECK(static_cast<uint8_t>(InflightState::WaitingForPubrel)  == 2U);
    CHECK(static_cast<uint8_t>(InflightState::WaitingForPubcomp) == 3U);
}

// ── InflightEntry (1.7.2) ─────────────────────────────────────────────────────

TEST_CASE("inflight_entry_defaults", "[session]")
{
    InflightEntry entry{};
    CHECK(entry.packet_id == 0U);
    CHECK(entry.qos       == QoS::AtLeastOnce);
    CHECK(entry.state     == InflightState::WaitingForPuback);
    CHECK(entry.direction == InflightDirection::Outbound);
}

TEST_CASE("inflight_entry_set_fields", "[session]")
{
    InflightEntry entry{};
    entry.packet_id             = 42U;
    entry.message.topic.value   = "sensors/temp";
    entry.qos                   = QoS::ExactlyOnce;
    entry.state                 = InflightState::WaitingForPubrec;
    entry.direction             = InflightDirection::Outbound;
    entry.timestamp             = std::chrono::steady_clock::now();

    CHECK(entry.packet_id              == 42U);
    CHECK(entry.message.topic.value    == "sensors/temp");
    CHECK(entry.qos                    == QoS::ExactlyOnce);
    CHECK(entry.state                  == InflightState::WaitingForPubrec);
    CHECK(entry.direction              == InflightDirection::Outbound);
}

TEST_CASE("inflight_entry_equality", "[session]")
{
    InflightEntry a{};
    InflightEntry b{};
    CHECK(a == b);
}

TEST_CASE("inflight_entry_inequality", "[session]")
{
    InflightEntry a{};
    InflightEntry b{};
    b.packet_id = 7U;
    CHECK(a != b);
}

TEST_CASE("inflight_entry_qos2_outbound", "[session]")
{
    InflightEntry entry{};
    entry.qos       = QoS::ExactlyOnce;
    entry.state     = InflightState::WaitingForPubrec;
    entry.direction = InflightDirection::Outbound;

    CHECK(entry.state     == InflightState::WaitingForPubrec);
    CHECK(entry.direction == InflightDirection::Outbound);
}

TEST_CASE("inflight_entry_qos2_inbound", "[session]")
{
    InflightEntry entry{};
    entry.qos       = QoS::ExactlyOnce;
    entry.state     = InflightState::WaitingForPubrel;
    entry.direction = InflightDirection::Inbound;

    CHECK(entry.state     == InflightState::WaitingForPubrel);
    CHECK(entry.direction == InflightDirection::Inbound);
}

// ── SessionState (1.7.1) ──────────────────────────────────────────────────────

TEST_CASE("session_state_defaults", "[session]")
{
    SessionState s{};
    CHECK(s.client_id.value.empty());
    CHECK(s.subscriptions.empty());
    CHECK(s.session_expiry_interval == 0U);
}

TEST_CASE("session_state_set_fields", "[session]")
{
    SessionState s{};
    s.client_id.value = "client-42";
    Subscription sub{};
    sub.topic_filter.value = "test/#";
    s.subscriptions.push_back(sub);
    s.session_expiry_interval = 3600U;

    CHECK(s.client_id.value                       == "client-42");
    CHECK(s.subscriptions.size()                  == 1U);
    CHECK(s.subscriptions[0].topic_filter.value   == "test/#");
    CHECK(s.session_expiry_interval               == 3600U);
}

TEST_CASE("session_state_equality", "[session]")
{
    SessionState a{};
    SessionState b{};
    CHECK(a == b);
}

TEST_CASE("session_state_inequality", "[session]")
{
    SessionState a{};
    SessionState b{};
    b.client_id.value = "other";
    CHECK(a != b);
}

TEST_CASE("session_state_never_expires", "[session]")
{
    SessionState s{};
    s.session_expiry_interval = 0xFFFF'FFFFU;
    CHECK(s.session_expiry_interval == 0xFFFF'FFFFU);
}
