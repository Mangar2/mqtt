#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <sstream>
#include <string>

#include "yaha/automation/expression_evaluator_helpers.h"

namespace {

constexpr double k_expected_numeric_value{123.5};
constexpr double k_numeric_value_other{123.5002};
constexpr double k_numeric_forty_two{42.0};
constexpr double k_numeric_twelve_point_five{12.5};
constexpr std::int64_t k_seconds_hour_minute_second{3723};

[[nodiscard]] yaha::RuntimeValue mapRuntimeValue() {
    yaha::MapRuntimeValue mapValue{};
    mapValue.entries.push_back(yaha::MapRuntimeEntry{
        .isDefault = true,
        .keyToken = "default",
        .value = std::string{"fallback"}});
    return yaha::RuntimeValue{mapValue};
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("expression_evaluator_helpers_parse_double_and_numeric_like_variants", "[yaha][automation]") {
    double parsedValue = 0.0;
    REQUIRE(yaha::parseDouble("123.5", &parsedValue));
    REQUIRE(parsedValue == k_expected_numeric_value);

    REQUIRE_FALSE(yaha::parseDouble("12x", &parsedValue));

    const auto numericDirect = yaha::tryNumericLikeJs(yaha::RuntimeValue{k_numeric_forty_two});
    REQUIRE(numericDirect.has_value());
    REQUIRE(*numericDirect == k_numeric_forty_two);

    const auto numericTrimmed = yaha::tryNumericLikeJs(yaha::RuntimeValue{std::string{"  123.5  "}});
    REQUIRE(numericTrimmed.has_value());
    REQUIRE(*numericTrimmed == k_expected_numeric_value);

    const auto numericEmpty = yaha::tryNumericLikeJs(yaha::RuntimeValue{std::string{"   "}});
    REQUIRE_FALSE(numericEmpty.has_value());

    const auto numericInvalid = yaha::tryNumericLikeJs(yaha::RuntimeValue{std::string{"abc"}});
    REQUIRE_FALSE(numericInvalid.has_value());

    const auto numericBool = yaha::tryNumericLikeJs(yaha::RuntimeValue{true});
    REQUIRE_FALSE(numericBool.has_value());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("expression_evaluator_helpers_parse_time_text_and_iso_offsets", "[yaha][automation]") {
    const auto hhmmss = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"01:02:03"}});
    REQUIRE(hhmmss.has_value());
    REQUIRE(hhmmss->count() == k_seconds_hour_minute_second);

    const auto hhmm = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"07:45"}});
    REQUIRE(hhmm.has_value());
    REQUIRE(hhmm->count() == 27900);

    const auto withZulu = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30Z"}});
    REQUIRE(withZulu.has_value());

    const auto withPositiveOffset = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T12:45:30+02:30"}});
    REQUIRE(withPositiveOffset.has_value());

    const auto withNegativeOffset = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T09:00:30-01:15"}});
    REQUIRE(withNegativeOffset.has_value());

    // These represent the same UTC instant and must resolve to the same local time-of-day.
    REQUIRE(*withPositiveOffset == *withZulu);
    REQUIRE(*withNegativeOffset == *withZulu);

    const auto withFractionOneDigit = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30.7Z"}});
    const auto withFractionTwoDigits = yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30.78Z"}});
    REQUIRE(withFractionOneDigit.has_value());
    REQUIRE(withFractionTwoDigits.has_value());
}

TEST_CASE("expression_evaluator_helpers_reject_invalid_iso_forms", "[yaha][automation]") {
    REQUIRE_FALSE(yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30.Z"}}).has_value());
    REQUIRE_FALSE(yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30+0200"}}).has_value());
    REQUIRE_FALSE(yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-13-07T10:15:30Z"}}).has_value());
    REQUIRE_FALSE(yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30Zx"}}).has_value());
    REQUIRE_FALSE(yaha::tryTimeOfDay(yaha::RuntimeValue{std::string{"2026-07-07T10:15:30+0A:00"}}).has_value());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("expression_evaluator_helpers_format_and_type_rendering", "[yaha][automation]") {
    REQUIRE(yaha::formatTimeText(std::chrono::seconds{-1}) == "23:59:59");

    const auto dayValue = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::July / 7};
    const yaha::RuntimeValue timeValue =
        yaha::RuntimeValue{std::chrono::system_clock::time_point{dayValue + std::chrono::hours{10}}};

    REQUIRE(yaha::valueToString(yaha::RuntimeValue{std::string{"hello"}}) == "hello");
    REQUIRE(yaha::valueToString(yaha::RuntimeValue{k_numeric_twelve_point_five}) == "12.5");
    REQUIRE(yaha::valueToString(yaha::RuntimeValue{true}) == "true");
    REQUIRE(yaha::valueToString(timeValue).size() == 8U);
    REQUIRE(yaha::valueToString(mapRuntimeValue()) == "map");

    REQUIRE(yaha::valueTypeToString(yaha::RuntimeValue{std::string{"x"}}) == "string");
    REQUIRE(yaha::valueTypeToString(yaha::RuntimeValue{1.0}) == "number");
    REQUIRE(yaha::valueTypeToString(yaha::RuntimeValue{false}) == "bool");
    REQUIRE(yaha::valueTypeToString(timeValue) == "time");
    REQUIRE(yaha::valueTypeToString(mapRuntimeValue()) == "map");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("expression_evaluator_helpers_bool_equals_and_error_cause", "[yaha][automation]") {
    REQUIRE_FALSE(yaha::toBool(yaha::RuntimeValue{std::string{"OFF"}}));
    REQUIRE(yaha::toBool(yaha::RuntimeValue{std::string{"maybe"}}));
    REQUIRE_FALSE(yaha::toBool(mapRuntimeValue()));

    REQUIRE(yaha::evalEquals(yaha::RuntimeValue{k_expected_numeric_value}, yaha::RuntimeValue{k_expected_numeric_value}));
    REQUIRE_FALSE(yaha::evalEquals(yaha::RuntimeValue{k_expected_numeric_value}, yaha::RuntimeValue{k_numeric_value_other}));

    const auto timeFromIsoZulu = yaha::RuntimeValue{std::string{"2026-07-07T10:15:30Z"}};
    const auto timeFromIsoOffset = yaha::RuntimeValue{std::string{"2026-07-07T12:45:30+02:30"}};
    REQUIRE(yaha::evalEquals(timeFromIsoZulu, timeFromIsoOffset));

    REQUIRE(yaha::relationOperatorSymbol("gt") == ">");
    REQUIRE(yaha::relationOperatorSymbol("lt") == "<");
    REQUIRE(yaha::relationOperatorSymbol("ge") == ">=");
    REQUIRE(yaha::relationOperatorSymbol("le") == "<=");
    REQUIRE(yaha::relationOperatorSymbol("unknown") == "?");

    REQUIRE(yaha::extractUndefinedVariableName("$SYS/temp (undefined)") == "$SYS/temp");
    REQUIRE(yaha::extractUndefinedVariableName("$SYS/temp") == "");

    std::ostringstream undefinedCauseText{};
    yaha::appendUndefinedCause(&undefinedCauseText, "", "$SYS/right");
    REQUIRE(undefinedCauseText.str().find("$SYS/right") != std::string::npos);
}
