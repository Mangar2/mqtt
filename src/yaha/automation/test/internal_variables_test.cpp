#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "yaha/automation/internal_variables.h"

namespace {

struct UtcDateTimeParts {
    int year;
    unsigned month;
    unsigned day;
    int hour;
    int minute;
    int second;
};

[[nodiscard]] yaha::InternalVariables::TimePoint makeUtcDate(
    const UtcDateTimeParts& parts) {
    const std::chrono::year_month_day ymd{
        std::chrono::year{parts.year} / std::chrono::month{parts.month} / std::chrono::day{parts.day}};
    return std::chrono::sys_days{ymd}
        + std::chrono::hours{parts.hour}
        + std::chrono::minutes{parts.minute}
        + std::chrono::seconds{parts.second};
}

[[nodiscard]] const std::chrono::system_clock::time_point& asTime(
    const yaha::InternalVariables::Value& value) {
    return std::get<std::chrono::system_clock::time_point>(value);
}

[[nodiscard]] double asNumber(const yaha::InternalVariables::Value& value) {
    return std::get<double>(value);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("internal_variables_returns_all_required_keys", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 13.4050, .latitude = 52.5200}};
    const auto date = makeUtcDate({.year = 2026, .month = 5, .day = 3, .hour = 12, .minute = 0, .second = 0});

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(map.size() == 10U);
    REQUIRE(map.contains("/time"));
    REQUIRE(map.contains("/weekday"));
    REQUIRE(map.contains("/sunrise"));
    REQUIRE(map.contains("/sunset"));
    REQUIRE(map.contains("/civildawn"));
    REQUIRE(map.contains("/civildusk"));
    REQUIRE(map.contains("/nauticaldawn"));
    REQUIRE(map.contains("/nauticaldusk"));
    REQUIRE(map.contains("/astronomicaldawn"));
    REQUIRE(map.contains("/astronomicaldusk"));
}

TEST_CASE("internal_variables_weekday_uses_sunday_zero_index", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 11.5761, .latitude = 48.1374}};
    const auto date = makeUtcDate({.year = 2026, .month = 5, .day = 3, .hour = 8, .minute = 30, .second = 0});

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(asNumber(map.at("/weekday")) == 0.0);
}

TEST_CASE("internal_variables_sun_times_have_expected_order", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 13.4050, .latitude = 52.5200}};
    const auto date = makeUtcDate({.year = 2026, .month = 3, .day = 21, .hour = 12, .minute = 0, .second = 0});

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(asTime(map.at("/astronomicaldawn")) <= asTime(map.at("/nauticaldawn")));
    REQUIRE(asTime(map.at("/nauticaldawn")) <= asTime(map.at("/civildawn")));
    REQUIRE(asTime(map.at("/civildawn")) <= asTime(map.at("/sunrise")));

    REQUIRE(asTime(map.at("/sunrise")) < asTime(map.at("/sunset")));

    REQUIRE(asTime(map.at("/sunset")) <= asTime(map.at("/civildusk")));
    REQUIRE(asTime(map.at("/civildusk")) <= asTime(map.at("/nauticaldusk")));
    REQUIRE(asTime(map.at("/nauticaldusk")) <= asTime(map.at("/astronomicaldusk")));
}

TEST_CASE("internal_variables_time_value_matches_input_date", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 8.6821, .latitude = 50.1109}};
    const auto date = makeUtcDate({.year = 2026, .month = 1, .day = 15, .hour = 5, .minute = 45, .second = 30});

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(asTime(map.at("/time")) == date);
}

TEST_CASE("internal_variables_reports_detailed_reason_when_sun_event_is_undefined", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 13.4050, .latitude = 90.0}};
    const auto date = makeUtcDate({.year = 2026, .month = 6, .day = 2, .hour = 12, .minute = 0, .second = 0});

    try {
        (void)variables.calculate(date);
        FAIL("expected sun-event calculation to fail for polar coordinates");
    } catch (const std::runtime_error& exceptionValue) {
        const std::string errorText = exceptionValue.what();
        REQUIRE(errorText.find("sun event calculation failed") != std::string::npos);
        REQUIRE(errorText.find("event=/") != std::string::npos);
        REQUIRE(errorText.find("cause=") != std::string::npos);
        REQUIRE(errorText.find("cosHourAngle=") != std::string::npos);
        REQUIRE(errorText.find("outside [-1") != std::string::npos);
        REQUIRE(errorText.find("latitude=90") != std::string::npos);
        REQUIRE(errorText.find("longitude=13.405") != std::string::npos);
        REQUIRE(errorText.find("utcDate=") != std::string::npos);
    }
}

TEST_CASE("internal_variables_keeps_twilight_keys_defined_for_central_europe_summer", "[yaha][automation]") {
    const yaha::InternalVariables variables{{.longitude = 8.205, .latitude = 49.912}};
    const auto date = makeUtcDate({.year = 2026, .month = 6, .day = 2, .hour = 12, .minute = 0, .second = 0});

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(map.contains("/sunrise"));
    REQUIRE(map.contains("/sunset"));
    REQUIRE(map.contains("/astronomicaldawn"));
    REQUIRE(map.contains("/astronomicaldusk"));
}

// NOLINTEND(readability-function-cognitive-complexity)
