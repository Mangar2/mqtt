#include <catch2/catch_test_macros.hpp>

#include "data_model/reason_code/reason_code.h"

using namespace mqtt;

// ── Enum values (1.2.1) ────────────────────────────────────────────────────────

TEST_CASE("rc_success_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::Success) == 0x00U);
}

TEST_CASE("rc_granted_qos1_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::GrantedQoS1) == 0x01U);
}

TEST_CASE("rc_granted_qos2_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::GrantedQoS2) == 0x02U);
}

TEST_CASE("rc_disconnect_with_will_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::DisconnectWithWill) == 0x04U);
}

TEST_CASE("rc_unspecified_error_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::UnspecifiedError) == 0x80U);
}

TEST_CASE("rc_wildcard_not_supported_value", "[reason_code]")
{
    STATIC_CHECK(static_cast<uint8_t>(ReasonCode::WildcardSubscriptionsNotSupported) == 0xA2U);
}

TEST_CASE("rc_aliases", "[reason_code]")
{
    STATIC_CHECK(k_normal_disconnection == ReasonCode::Success);
    STATIC_CHECK(k_granted_qos0        == ReasonCode::Success);
}

// ── Classification (1.2.2) ────────────────────────────────────────────────────

TEST_CASE("rc_is_success_true", "[reason_code]")
{
    STATIC_CHECK(is_success(ReasonCode::Success));
    STATIC_CHECK(is_success(ReasonCode::GrantedQoS1));
    STATIC_CHECK(is_success(ReasonCode::GrantedQoS2));
    STATIC_CHECK(is_success(ReasonCode::ContinueAuthentication));
    STATIC_CHECK(is_success(ReasonCode::ReAuthenticate));
}

TEST_CASE("rc_is_success_false", "[reason_code]")
{
    STATIC_CHECK(!is_success(ReasonCode::UnspecifiedError));
    STATIC_CHECK(!is_success(ReasonCode::NotAuthorized));
}

TEST_CASE("rc_is_error_true", "[reason_code]")
{
    STATIC_CHECK(is_error(ReasonCode::UnspecifiedError));
    STATIC_CHECK(is_error(ReasonCode::NotAuthorized));
    STATIC_CHECK(is_error(ReasonCode::WildcardSubscriptionsNotSupported));
}

TEST_CASE("rc_is_error_false", "[reason_code]")
{
    STATIC_CHECK(!is_error(ReasonCode::Success));
    STATIC_CHECK(!is_error(ReasonCode::GrantedQoS1));
}

TEST_CASE("rc_boundary_0x7F", "[reason_code]")
{
    constexpr auto rc = static_cast<ReasonCode>(0x7FU);
    STATIC_CHECK(is_success(rc));
    STATIC_CHECK(!is_error(rc));
}

TEST_CASE("rc_boundary_0x80", "[reason_code]")
{
    constexpr auto rc = static_cast<ReasonCode>(0x80U);
    STATIC_CHECK(is_error(rc));
    STATIC_CHECK(!is_success(rc));
}
