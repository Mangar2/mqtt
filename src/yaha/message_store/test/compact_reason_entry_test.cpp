#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/message_store/compact_reason_entry.h"

namespace {

constexpr const char* k_plain_timestamp{"2026-01-01T00:00:00Z"};
constexpr const char* k_one_digit_fraction_timestamp{"2026-01-01T00:00:00.1Z"};
constexpr const char* k_two_digit_fraction_timestamp{"2026-01-01T00:00:00.12Z"};
constexpr const char* k_three_digit_fraction_timestamp{"2026-01-01T00:00:00.123Z"};
constexpr const char* k_plus_offset_fraction_timestamp{"2026-01-01T00:00:00.12+01:00"};
constexpr const char* k_minus_offset_fraction_timestamp{"2026-01-01T00:00:00.12-01:00"};
constexpr const char* k_four_digit_zero_fraction_timestamp{"2026-01-01T00:00:00.0001Z"};

} // namespace

TEST_CASE("compact_reason_entry_parses_plain_iso_timestamp", "[message_store]") {
    const yaha::CompactReasonEntry entry =
        yaha::CompactReasonEntry::fromStrings("reason", k_plain_timestamp);

    REQUIRE(entry.message() == "reason");
    REQUIRE(entry.timestampMs() != 0);
    REQUIRE(entry.fractionalDigits() == 0U);
    REQUIRE(entry.toTimestampString() == k_plain_timestamp);
}

TEST_CASE("compact_reason_entry_keeps_fractional_precision_1_to_3_digits", "[message_store]") {
    const yaha::CompactReasonEntry oneDigit =
        yaha::CompactReasonEntry::fromStrings("reason", k_one_digit_fraction_timestamp);
    const yaha::CompactReasonEntry twoDigits =
        yaha::CompactReasonEntry::fromStrings("reason", k_two_digit_fraction_timestamp);
    const yaha::CompactReasonEntry threeDigits =
        yaha::CompactReasonEntry::fromStrings("reason", k_three_digit_fraction_timestamp);

    REQUIRE(oneDigit.fractionalDigits() == 1U);
    REQUIRE(twoDigits.fractionalDigits() == 2U);
    REQUIRE(threeDigits.fractionalDigits() == 3U);

    REQUIRE(oneDigit.toTimestampString() == k_one_digit_fraction_timestamp);
    REQUIRE(twoDigits.toTimestampString() == k_two_digit_fraction_timestamp);
    REQUIRE(threeDigits.toTimestampString() == k_three_digit_fraction_timestamp);
}

TEST_CASE("compact_reason_entry_counts_fraction_digits_with_plus_offset", "[message_store]") {
    const yaha::CompactReasonEntry entry =
        yaha::CompactReasonEntry::fromStrings("reason", k_plus_offset_fraction_timestamp);

    REQUIRE(entry.timestampMs() != 0);
    REQUIRE(entry.fractionalDigits() == 2U);
    REQUIRE(entry.toTimestampString().ends_with(".12Z"));
}

TEST_CASE("compact_reason_entry_counts_fraction_digits_with_minus_offset", "[message_store]") {
    const yaha::CompactReasonEntry entry =
        yaha::CompactReasonEntry::fromStrings("reason", k_minus_offset_fraction_timestamp);

    REQUIRE(entry.timestampMs() != 0);
    REQUIRE(entry.fractionalDigits() == 2U);
    REQUIRE(entry.toTimestampString().ends_with(".12Z"));
}

TEST_CASE("compact_reason_entry_inserts_fraction_when_metadata_requests_it", "[message_store]") {
    const yaha::CompactReasonEntry entry =
        yaha::CompactReasonEntry::fromStrings("reason", k_four_digit_zero_fraction_timestamp);

    REQUIRE(entry.timestampMs() != 0);
    REQUIRE(entry.fractionalDigits() == 4U);
    REQUIRE(entry.toTimestampString().ends_with(".000Z"));
}

TEST_CASE("compact_reason_entry_returns_empty_timestamp_when_parse_fails", "[message_store]") {
    const yaha::CompactReasonEntry entry =
        yaha::CompactReasonEntry::fromStrings("reason", "invalid");

    REQUIRE(entry.timestampMs() == 0);
    REQUIRE(entry.fractionalDigits() == 0U);
    REQUIRE(entry.toTimestampString().empty());
}
