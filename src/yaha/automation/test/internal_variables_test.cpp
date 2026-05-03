#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "yaha/automation/internal_variables.h"

namespace {

[[nodiscard]] yaha::InternalVariables::TimePoint makeUtcDate(
    const int year,
    const unsigned month,
    const unsigned day,
    const int hour,
    const int minute,
    const int second) {
    const std::chrono::year_month_day ymd{
        std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}};
    return std::chrono::sys_days{ymd}
        + std::chrono::hours{hour}
        + std::chrono::minutes{minute}
        + std::chrono::seconds{second};
}

[[nodiscard]] const std::chrono::system_clock::time_point& asTime(
    const yaha::InternalVariables::Value& value) {
    return std::get<std::chrono::system_clock::time_point>(value);
}

[[nodiscard]] double asNumber(const yaha::InternalVariables::Value& value) {
    return std::get<double>(value);
}

} // namespace

TEST_CASE("internal_variables_returns_all_required_keys", "[yaha][automation]") {
    const yaha::InternalVariables variables{13.4050, 52.5200};
    const auto date = makeUtcDate(2026, 5, 3, 12, 0, 0);

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
    const yaha::InternalVariables variables{11.5761, 48.1374};
    const auto date = makeUtcDate(2026, 5, 3, 8, 30, 0);

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(asNumber(map.at("/weekday")) == 0.0);
}

TEST_CASE("internal_variables_sun_times_have_expected_order", "[yaha][automation]") {
    const yaha::InternalVariables variables{13.4050, 52.5200};
    const auto date = makeUtcDate(2026, 3, 21, 12, 0, 0);

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
    const yaha::InternalVariables variables{8.6821, 50.1109};
    const auto date = makeUtcDate(2026, 1, 15, 5, 45, 30);

    const yaha::InternalVariables::VariableMap map = variables.calculate(date);

    REQUIRE(asTime(map.at("/time")) == date);
}
