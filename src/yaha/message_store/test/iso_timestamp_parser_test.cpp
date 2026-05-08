#include <catch2/catch_test_macros.hpp>

#include "yaha/message_store/iso_timestamp_parser.h"

#include <cstdint>
#include <string>

namespace {

TEST_CASE("iso_parser_accepts_leap_day_and_roundtrips", "[message_store]") {
    std::int64_t parsedMilliseconds = 0;
    REQUIRE(yaha::tryParseIsoTimestampMilliseconds("2024-02-29T12:34:56Z", parsedMilliseconds));
    REQUIRE(yaha::toIsoTimestampMilliseconds(parsedMilliseconds) == "2024-02-29T12:34:56.000Z");
}

TEST_CASE("iso_parser_rejects_non_leap_february_29", "[message_store]") {
    std::int64_t parsedMilliseconds = 0;
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2023-02-29T00:00:00Z", parsedMilliseconds));
}

TEST_CASE("iso_parser_rejects_invalid_month_and_day_combinations", "[message_store]") {
    std::int64_t parsedMilliseconds = 0;
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2024-13-01T00:00:00Z", parsedMilliseconds));
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2024-04-31T00:00:00Z", parsedMilliseconds));
}

TEST_CASE("iso_parser_rejects_invalid_timezone_ranges", "[message_store]") {
    std::int64_t parsedMilliseconds = 0;
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2024-01-01T00:00:00+24:00", parsedMilliseconds));
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2024-01-01T00:00:00+01:60", parsedMilliseconds));
}

TEST_CASE("iso_parser_rejects_trailing_characters", "[message_store]") {
    std::int64_t parsedMilliseconds = 0;
    REQUIRE_FALSE(yaha::tryParseIsoTimestampMilliseconds("2024-01-01T00:00:00Z trailing", parsedMilliseconds));
}

TEST_CASE("iso_formatter_handles_negative_milliseconds", "[message_store]") {
    const std::string formatted = yaha::toIsoTimestampMilliseconds(-1);
    REQUIRE(formatted == "1969-12-31T23:59:59.999Z");
}

} // namespace
