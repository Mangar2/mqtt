#include "yaha/message_store/iso_timestamp_parser.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace yaha {

namespace {

constexpr int k_millis_per_second{1000};
constexpr int k_seconds_per_minute{60};
constexpr int k_minutes_per_hour{60};
constexpr int k_hours_per_day{24};
constexpr int k_leap_cycle_divisor{4};
constexpr int k_century_divisor{100};
constexpr int k_quad_century_divisor{400};
constexpr int k_month_count{12};
constexpr int k_min_month{1};
constexpr int k_february_month{2};
constexpr int k_february_days_in_leap_year{29};
constexpr int k_max_hour{23};
constexpr int k_max_minute_or_second{59};
constexpr int k_iso_utc_hour_limit{23};
constexpr int k_iso_utc_minute_limit{59};
constexpr int k_decimal_base{10};
constexpr std::size_t k_iso_min_length{20U};
constexpr std::size_t k_fraction_scale_digits{3U};
constexpr std::size_t k_two_digits{2U};
constexpr std::size_t k_iso_year_start{0U};
constexpr std::size_t k_iso_year_digits{4U};
constexpr std::size_t k_iso_month_start{5U};
constexpr std::size_t k_iso_day_start{8U};
constexpr std::size_t k_iso_hour_start{11U};
constexpr std::size_t k_iso_minute_start{14U};
constexpr std::size_t k_iso_second_start{17U};
constexpr std::size_t k_iso_after_second_start{19U};
constexpr std::size_t k_iso_month_separator_pos{4U};
constexpr std::size_t k_iso_day_separator_pos{7U};
constexpr std::size_t k_iso_date_time_separator_pos{10U};
constexpr std::size_t k_iso_hour_separator_pos{13U};
constexpr std::size_t k_iso_minute_separator_pos{16U};
constexpr int k_days_per_year{365};
constexpr int k_days_per_era{146097};
constexpr int k_unix_epoch_civil_offset_days{719468};
constexpr int k_day_formula_factor{153};
constexpr int k_day_formula_add{2};
constexpr int k_day_formula_divisor{5};
constexpr int k_month_adjust_after_february{-3};
constexpr int k_month_adjust_before_or_equal_february{9};
constexpr int k_tm_year_offset{1900};

constexpr std::array<int, k_month_count> k_days_per_month{
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

struct ParsedIsoTimestamp {
    int year{0};
    int month{0};
    int day{0};
    int hour{0};
    int minute{0};
    int second{0};
    int millis{0};
    int timezoneOffsetMinutes{0};
};

[[nodiscard]] bool isLeapYear(const int year) {
    return (year % k_leap_cycle_divisor == 0 && year % k_century_divisor != 0) ||
           year % k_quad_century_divisor == 0;
}

[[nodiscard]] int daysInMonth(const int year, const int month) {
    if (month < k_min_month || month > k_month_count) {
        return 0;
    }

    if (month == k_february_month && isLeapYear(year)) {
        return k_february_days_in_leap_year;
    }

    return k_days_per_month[static_cast<std::size_t>(month - k_min_month)];
}

[[nodiscard]] bool parseFixedDigits(const std::string& text,
                                    const std::size_t start,
                                    const std::size_t amount,
                                    int& parsedValue) {
    if (start + amount > text.size()) {
        return false;
    }

    int value = 0;
    for (std::size_t idx = 0U; idx < amount; ++idx) {
        const char chr = text[start + idx];
        if (std::isdigit(static_cast<unsigned char>(chr)) == 0) {
            return false;
        }
        value = (value * k_decimal_base) + (chr - '0');
    }

    parsedValue = value;
    return true;
}

[[nodiscard]] std::int64_t daysFromCivil(int year, const unsigned int month, const unsigned int day) {
    year -= month <= 2U ? 1 : 0;
    const int era = (year >= 0 ? year : year - (k_quad_century_divisor - 1)) / k_quad_century_divisor;
    const unsigned int yearOfEra = static_cast<unsigned int>(year - (era * k_quad_century_divisor));
    const int monthForFormula = static_cast<int>(month);
    const int monthAdjustment = monthForFormula > k_february_month
        ? k_month_adjust_after_february
        : k_month_adjust_before_or_equal_february;
    const unsigned int dayOfYear =
        static_cast<unsigned int>((k_day_formula_factor * (monthForFormula + monthAdjustment) +
                                   k_day_formula_add) / k_day_formula_divisor) +
        day - 1U;
    const unsigned int dayOfEra =
        (yearOfEra * static_cast<unsigned int>(k_days_per_year)) +
        (yearOfEra / static_cast<unsigned int>(k_leap_cycle_divisor)) -
        (yearOfEra / static_cast<unsigned int>(k_century_divisor)) +
        dayOfYear;
    return (static_cast<std::int64_t>(era) * k_days_per_era) +
           static_cast<std::int64_t>(dayOfEra) -
           k_unix_epoch_civil_offset_days;
}

[[nodiscard]] bool hasIsoDateTimeLayout(const std::string& timestampText) {
    return timestampText[k_iso_month_separator_pos] == '-' &&
           timestampText[k_iso_day_separator_pos] == '-' &&
           timestampText[k_iso_date_time_separator_pos] == 'T' &&
           timestampText[k_iso_hour_separator_pos] == ':' &&
           timestampText[k_iso_minute_separator_pos] == ':';
}

[[nodiscard]] bool parseIsoDateTimeFields(const std::string& timestampText, ParsedIsoTimestamp& parsed) {
    if (!parseFixedDigits(timestampText, k_iso_year_start, k_iso_year_digits, parsed.year) ||
        !parseFixedDigits(timestampText, k_iso_month_start, k_two_digits, parsed.month) ||
        !parseFixedDigits(timestampText, k_iso_day_start, k_two_digits, parsed.day) ||
        !parseFixedDigits(timestampText, k_iso_hour_start, k_two_digits, parsed.hour) ||
        !parseFixedDigits(timestampText, k_iso_minute_start, k_two_digits, parsed.minute) ||
        !parseFixedDigits(timestampText, k_iso_second_start, k_two_digits, parsed.second)) {
        return false;
    }

    if (parsed.month < k_min_month || parsed.month > k_month_count) {
        return false;
    }
    if (parsed.day < 1 || parsed.day > daysInMonth(parsed.year, parsed.month)) {
        return false;
    }

    if (parsed.hour < 0 || parsed.hour > k_max_hour ||
        parsed.minute < 0 || parsed.minute > k_max_minute_or_second ||
        parsed.second < 0 || parsed.second > k_max_minute_or_second) {
        return false;
    }

    return true;
}

[[nodiscard]] bool parseIsoFractionMilliseconds(const std::string& timestampText,
                                                std::size_t& cursor,
                                                ParsedIsoTimestamp& parsed) {
    if (cursor >= timestampText.size() || timestampText[cursor] != '.') {
        return true;
    }

    cursor += 1U;
    const std::size_t fractionStart = cursor;
    while (cursor < timestampText.size() &&
           std::isdigit(static_cast<unsigned char>(timestampText[cursor])) != 0) {
        cursor += 1U;
    }

    const std::size_t fractionDigits = cursor - fractionStart;
    if (fractionDigits == 0U) {
        return false;
    }

    parsed.millis = 0;
    for (std::size_t idx = 0U; idx < k_fraction_scale_digits; ++idx) {
        parsed.millis *= k_decimal_base;
        if (idx < fractionDigits) {
            parsed.millis += static_cast<int>(timestampText[fractionStart + idx] - '0');
        }
    }

    return true;
}

[[nodiscard]] bool parseIsoTimezoneOffset(const std::string& timestampText,
                                          std::size_t& cursor,
                                          ParsedIsoTimestamp& parsed) {
    if (cursor >= timestampText.size()) {
        return false;
    }

    if (timestampText[cursor] == 'Z') {
        cursor += 1U;
        parsed.timezoneOffsetMinutes = 0;
        return true;
    }

    if (timestampText[cursor] != '+' && timestampText[cursor] != '-') {
        return false;
    }

    const bool positiveOffset = timestampText[cursor] == '+';
    cursor += 1U;

    int offsetHour = 0;
    int offsetMinute = 0;
    if (!parseFixedDigits(timestampText, cursor, k_two_digits, offsetHour)) {
        return false;
    }
    cursor += k_two_digits;

    if (cursor >= timestampText.size() || timestampText[cursor] != ':') {
        return false;
    }
    cursor += 1U;

    if (!parseFixedDigits(timestampText, cursor, k_two_digits, offsetMinute)) {
        return false;
    }
    cursor += k_two_digits;

    if (offsetHour > k_iso_utc_hour_limit || offsetMinute > k_iso_utc_minute_limit) {
        return false;
    }

    parsed.timezoneOffsetMinutes = offsetHour * k_minutes_per_hour + offsetMinute;
    if (!positiveOffset) {
        parsed.timezoneOffsetMinutes = -parsed.timezoneOffsetMinutes;
    }

    return true;
}

} // namespace

bool tryParseIsoTimestampMilliseconds(const std::string& timestampText,
                                      std::int64_t& outMilliseconds) {
    if (timestampText.size() < k_iso_min_length || !hasIsoDateTimeLayout(timestampText)) {
        return false;
    }

    ParsedIsoTimestamp parsed{};
    if (!parseIsoDateTimeFields(timestampText, parsed)) {
        return false;
    }

    std::size_t cursor = k_iso_after_second_start;
    if (!parseIsoFractionMilliseconds(timestampText, cursor, parsed)) {
        return false;
    }

    if (!parseIsoTimezoneOffset(timestampText, cursor, parsed)) {
        return false;
    }
    if (cursor != timestampText.size()) {
        return false;
    }

    const std::int64_t days = daysFromCivil(parsed.year,
                                            static_cast<unsigned int>(parsed.month),
                                            static_cast<unsigned int>(parsed.day));
    const std::int64_t totalHours = (days * k_hours_per_day) + parsed.hour;
    const std::int64_t totalMinutes = (totalHours * k_minutes_per_hour) + parsed.minute;
    const std::int64_t totalSeconds = (totalMinutes * k_seconds_per_minute) + parsed.second;

    outMilliseconds = (totalSeconds * k_millis_per_second) + parsed.millis -
        (static_cast<std::int64_t>(parsed.timezoneOffsetMinutes) *
         k_seconds_per_minute *
         k_millis_per_second);
    return true;
}

std::string toIsoTimestampMilliseconds(std::int64_t millisecondsSinceEpoch) {
    std::int64_t secondsSinceEpoch = millisecondsSinceEpoch / k_millis_per_second;
    std::int64_t millisecondPart = millisecondsSinceEpoch % k_millis_per_second;
    if (millisecondPart < 0) {
        millisecondPart += k_millis_per_second;
        secondsSinceEpoch -= 1;
    }

    const std::time_t rawTime = static_cast<std::time_t>(secondsSinceEpoch);
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &rawTime) != 0) {
        return "1970-01-01T00:00:00.000Z";
    }
#else
    if (gmtime_r(&rawTime, &utc) == nullptr) {
        return "1970-01-01T00:00:00.000Z";
    }
#endif

    std::ostringstream stream{};
    stream << std::setfill('0')
           << std::setw(4) << (utc.tm_year + k_tm_year_offset)
           << '-' << std::setw(2) << (utc.tm_mon + 1)
           << '-' << std::setw(2) << utc.tm_mday
           << 'T' << std::setw(2) << utc.tm_hour
           << ':' << std::setw(2) << utc.tm_min
           << ':' << std::setw(2) << utc.tm_sec
           << '.' << std::setw(3) << millisecondPart
           << 'Z';
    return stream.str();
}

} // namespace yaha
