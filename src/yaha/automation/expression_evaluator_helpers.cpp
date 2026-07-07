#include "yaha/automation/expression_evaluator_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace yaha {
namespace {

constexpr int k_max_minutes_or_seconds{59};
constexpr int k_max_hours{23};
constexpr int k_decimal_base{10};
constexpr int k_millis_scale_one_digit{100};
constexpr int k_millis_scale_two_digits{10};
constexpr int k_seconds_per_hour{3600};
constexpr int k_seconds_per_minute{60};

constexpr std::size_t k_iso_min_length{20U};
constexpr std::size_t k_iso_year_index{0U};
constexpr std::size_t k_iso_month_index{5U};
constexpr std::size_t k_iso_day_index{8U};
constexpr std::size_t k_iso_hour_index{11U};
constexpr std::size_t k_iso_minute_index{14U};
constexpr std::size_t k_iso_second_index{17U};
constexpr std::size_t k_iso_date_separator_1_index{4U};
constexpr std::size_t k_iso_date_separator_2_index{7U};
constexpr std::size_t k_iso_time_separator_index{10U};
constexpr std::size_t k_iso_hour_separator_index{13U};
constexpr std::size_t k_iso_minute_separator_index{16U};
constexpr std::size_t k_iso_fraction_start_index{19U};
constexpr std::size_t k_iso_year_length{4U};
constexpr std::size_t k_iso_two_digit_length{2U};
constexpr std::size_t k_iso_max_millis_digits{3U};

struct IsoInstantComponents {
    int yearValue{0};
    int monthValue{0};
    int dayValue{0};
    int hourValue{0};
    int minuteValue{0};
    int secondValue{0};
    int millisecondValue{0};
    int timezoneOffsetSeconds{0};
};

[[nodiscard]] bool parseFixedUnsignedInt(
    const std::string& textValue,
    const std::size_t startIndex,
    const std::size_t length,
    int* parsedValue) {
    if (startIndex + length > textValue.size() || length == 0U) {
        return false;
    }

    int value = 0;
    for (std::size_t index = 0U; index < length; ++index) {
        const char currentChar = textValue[startIndex + index];
        if (currentChar < '0' || currentChar > '9') {
            return false;
        }
        value = value * k_decimal_base + (currentChar - '0');
    }

    *parsedValue = value;
    return true;
}

[[nodiscard]] bool parseIsoDateTimePrefix(
    const std::string& textValue,
    IsoInstantComponents* components,
    std::size_t* cursorIndex) {
    if (textValue.size() < k_iso_min_length) {
        return false;
    }
    if (textValue[k_iso_date_separator_1_index] != '-'
        || textValue[k_iso_date_separator_2_index] != '-'
        || textValue[k_iso_time_separator_index] != 'T'
        || textValue[k_iso_hour_separator_index] != ':'
        || textValue[k_iso_minute_separator_index] != ':') {
        return false;
    }

    if (!parseFixedUnsignedInt(textValue, k_iso_year_index, k_iso_year_length, &components->yearValue)
        || !parseFixedUnsignedInt(textValue, k_iso_month_index, k_iso_two_digit_length, &components->monthValue)
        || !parseFixedUnsignedInt(textValue, k_iso_day_index, k_iso_two_digit_length, &components->dayValue)
        || !parseFixedUnsignedInt(textValue, k_iso_hour_index, k_iso_two_digit_length, &components->hourValue)
        || !parseFixedUnsignedInt(textValue, k_iso_minute_index, k_iso_two_digit_length, &components->minuteValue)
        || !parseFixedUnsignedInt(textValue, k_iso_second_index, k_iso_two_digit_length, &components->secondValue)) {
        return false;
    }

    if (components->hourValue < 0 || components->hourValue > k_max_hours
        || components->minuteValue < 0 || components->minuteValue > k_max_minutes_or_seconds
        || components->secondValue < 0 || components->secondValue > k_max_minutes_or_seconds) {
        return false;
    }

    *cursorIndex = k_iso_fraction_start_index;
    return true;
}

[[nodiscard]] bool parseIsoOptionalFraction(
    const std::string& textValue,
    std::size_t* cursorIndex,
    IsoInstantComponents* components) {
    if (*cursorIndex >= textValue.size() || textValue[*cursorIndex] != '.') {
        return true;
    }

    *cursorIndex += 1U;
    const std::size_t fractionStart = *cursorIndex;
    while (*cursorIndex < textValue.size() && std::isdigit(static_cast<unsigned char>(textValue[*cursorIndex])) != 0) {
        *cursorIndex += 1U;
    }

    const std::size_t fractionLength = *cursorIndex - fractionStart;
    if (fractionLength == 0U) {
        return false;
    }

    int fractionAccumulator = 0;
    const std::size_t digitsToUse = std::min<std::size_t>(k_iso_max_millis_digits, fractionLength);
    for (std::size_t index = 0U; index < digitsToUse; ++index) {
        fractionAccumulator = fractionAccumulator * k_decimal_base + (textValue[fractionStart + index] - '0');
    }

    if (digitsToUse == 1U) {
        fractionAccumulator *= k_millis_scale_one_digit;
    } else if (digitsToUse == 2U) {
        fractionAccumulator *= k_millis_scale_two_digits;
    }

    components->millisecondValue = fractionAccumulator;
    return true;
}

[[nodiscard]] bool parseIsoTimezoneOffset(
    const std::string& textValue,
    std::size_t* cursorIndex,
    IsoInstantComponents* components) {
    if (*cursorIndex >= textValue.size()) {
        return false;
    }

    if (textValue[*cursorIndex] == 'Z') {
        *cursorIndex += 1U;
        components->timezoneOffsetSeconds = 0;
        return true;
    }

    if (textValue[*cursorIndex] != '+' && textValue[*cursorIndex] != '-') {
        return false;
    }

    const bool isNegativeOffset = textValue[*cursorIndex] == '-';
    *cursorIndex += 1U;

    int offsetHourValue = 0;
    int offsetMinuteValue = 0;
    if (!parseFixedUnsignedInt(textValue, *cursorIndex, k_iso_two_digit_length, &offsetHourValue)) {
        return false;
    }
    *cursorIndex += k_iso_two_digit_length;
    if (*cursorIndex >= textValue.size() || textValue[*cursorIndex] != ':') {
        return false;
    }
    *cursorIndex += 1U;
    if (!parseFixedUnsignedInt(textValue, *cursorIndex, k_iso_two_digit_length, &offsetMinuteValue)) {
        return false;
    }
    *cursorIndex += k_iso_two_digit_length;

    components->timezoneOffsetSeconds = offsetHourValue * k_seconds_per_hour + offsetMinuteValue * k_seconds_per_minute;
    if (isNegativeOffset) {
        components->timezoneOffsetSeconds = -components->timezoneOffsetSeconds;
    }
    return true;
}

[[nodiscard]] bool tryParseIsoInstantText(
    const std::string& textValue,
    std::chrono::system_clock::time_point* parsedTimePoint) {
    IsoInstantComponents components;
    std::size_t cursorIndex = 0U;
    if (!parseIsoDateTimePrefix(textValue, &components, &cursorIndex)) {
        return false;
    }
    if (!parseIsoOptionalFraction(textValue, &cursorIndex, &components)) {
        return false;
    }
    if (!parseIsoTimezoneOffset(textValue, &cursorIndex, &components)) {
        return false;
    }

    if (cursorIndex != textValue.size()) {
        return false;
    }

    const auto yearMonthDay = std::chrono::year{components.yearValue}
        / std::chrono::month{static_cast<unsigned>(components.monthValue)}
        / std::chrono::day{static_cast<unsigned>(components.dayValue)};
    if (!yearMonthDay.ok()) {
        return false;
    }

    const auto dayStart = std::chrono::sys_days{yearMonthDay};
    auto timePoint = std::chrono::system_clock::time_point{dayStart}
        + std::chrono::hours{components.hourValue}
        + std::chrono::minutes{components.minuteValue}
        + std::chrono::seconds{components.secondValue}
        + std::chrono::milliseconds{components.millisecondValue};

    timePoint -= std::chrono::seconds{components.timezoneOffsetSeconds};
    *parsedTimePoint = timePoint;
    return true;
}

[[nodiscard]] std::string trimWhitespace(const std::string& textValue) {
    std::size_t beginIndex = 0U;
    while (beginIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    std::size_t endIndex = textValue.size();
    while (endIndex > beginIndex
        && std::isspace(static_cast<unsigned char>(textValue[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return textValue.substr(beginIndex, endIndex - beginIndex);
}

[[nodiscard]] bool tryParseTimeText(const std::string& timeText, std::chrono::seconds* parsedSeconds) {
    int hourValue = -1;
    int minuteValue = -1;
    int secondValue = 0;

    if (std::sscanf(timeText.c_str(), "%d:%d:%d", &hourValue, &minuteValue, &secondValue) == 3) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds || secondValue < 0
            || secondValue > k_max_minutes_or_seconds) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue} + std::chrono::seconds{secondValue};
        return true;
    }

    if (std::sscanf(timeText.c_str(), "%d:%d", &hourValue, &minuteValue) == 2) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue};
        return true;
    }

    return false;
}

[[nodiscard]] std::optional<std::tm> toLocalCalendarTime(
    const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    if (localtime_s(&localCalendarTime, &epochSeconds) != 0) {
        return std::nullopt;
    }
#else
    if (localtime_r(&epochSeconds, &localCalendarTime) == nullptr) {
        return std::nullopt;
    }
#endif
    return localCalendarTime;
}

[[nodiscard]] std::optional<std::chrono::seconds> localTimeOfDay(
    const std::chrono::system_clock::time_point& timePoint) {
    const auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    return std::chrono::hours{localCalendarTime->tm_hour}
        + std::chrono::minutes{localCalendarTime->tm_min}
        + std::chrono::seconds{localCalendarTime->tm_sec};
}

[[nodiscard]] bool isUndefinedReason(const std::string& reasonText) {
    constexpr std::string_view undefinedSuffix{" (undefined)"};
    return reasonText.ends_with(undefinedSuffix);
}

} // namespace

std::string toLower(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](const unsigned char charValue) {
        return static_cast<char>(std::tolower(charValue));
    });
    return textValue;
}

bool parseDouble(const std::string& tokenText, double* parsedValue) {
    std::size_t parsedLength = 0U;
    try {
        *parsedValue = std::stod(tokenText, &parsedLength);
    } catch (...) {
        return false;
    }
    return parsedLength == tokenText.size();
}

std::optional<double> tryNumericLikeJs(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<double>(runtimeValue)) {
        return std::get<double>(runtimeValue);
    }
    if (!std::holds_alternative<std::string>(runtimeValue)) {
        return std::nullopt;
    }

    const std::string trimmedText = trimWhitespace(std::get<std::string>(runtimeValue));
    if (trimmedText.empty()) {
        return std::nullopt;
    }

    double parsedValue = 0.0;
    if (!parseDouble(trimmedText, &parsedValue)) {
        return std::nullopt;
    }
    return parsedValue;
}

std::optional<std::chrono::system_clock::time_point> localDayStart(
    const std::chrono::system_clock::time_point& timePoint) {
    auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    localCalendarTime->tm_hour = 0;
    localCalendarTime->tm_min = 0;
    localCalendarTime->tm_sec = 0;
    localCalendarTime->tm_isdst = -1;
    const std::time_t localMidnightSeconds = std::mktime(&*localCalendarTime);
    if (localMidnightSeconds == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(localMidnightSeconds);
}

std::optional<std::chrono::seconds> tryTimeOfDay(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        return localTimeOfDay(std::get<std::chrono::system_clock::time_point>(runtimeValue));
    }

    if (std::holds_alternative<std::string>(runtimeValue)) {
        const std::string stringValue = std::get<std::string>(runtimeValue);
        std::chrono::seconds parsedSeconds{};
        if (tryParseTimeText(stringValue, &parsedSeconds)) {
            return parsedSeconds;
        }

        std::chrono::system_clock::time_point parsedTimePoint{};
        if (tryParseIsoInstantText(stringValue, &parsedTimePoint)) {
            return localTimeOfDay(parsedTimePoint);
        }
    }

    return std::nullopt;
}

std::string formatTimeText(const std::chrono::seconds secondsValue) {
    const auto secondCount = secondsValue.count();
    const auto normalizedSeconds = (secondCount % 86400 + 86400) % 86400;
    const int hourValue = static_cast<int>(normalizedSeconds / 3600);
    const int minuteValue = static_cast<int>((normalizedSeconds % 3600) / 60);
    const int secondValue = static_cast<int>(normalizedSeconds % 60);

    std::ostringstream textStream;
    textStream.fill('0');
    textStream.width(2);
    textStream << hourValue << ':';
    textStream.width(2);
    textStream << minuteValue << ':';
    textStream.width(2);
    textStream << secondValue;
    return textStream.str();
}

std::string valueToString(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::string>(runtimeValue)) {
        return std::get<std::string>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        std::ostringstream textStream;
        textStream << std::get<double>(runtimeValue);
        return textStream.str();
    }
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue) ? "true" : "false";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        const auto secondsValue = tryTimeOfDay(runtimeValue);
        return secondsValue.has_value() ? formatTimeText(*secondsValue) : "time";
    }
    return "map";
}

std::string valueTypeToString(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::string>(runtimeValue)) {
        return "string";
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        return "number";
    }
    if (std::holds_alternative<bool>(runtimeValue)) {
        return "bool";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        return "time";
    }
    return "map";
}

std::string extractUndefinedVariableName(const std::string& reasonText) {
    constexpr std::string_view undefinedSuffix{" (undefined)"};
    if (!isUndefinedReason(reasonText)) {
        return {};
    }
    return reasonText.substr(0U, reasonText.size() - undefinedSuffix.size());
}

std::string relationOperatorSymbol(const std::string& relationOperator) {
    if (relationOperator == "gt") {
        return ">";
    }
    if (relationOperator == "lt") {
        return "<";
    }
    if (relationOperator == "ge") {
        return ">=";
    }
    if (relationOperator == "le") {
        return "<=";
    }
    return "?";
}

void appendUndefinedCause(
    std::ostringstream* errorText,
    const std::string& leftUndefinedVariable,
    const std::string& rightUndefinedVariable) {
    if (leftUndefinedVariable.empty() && rightUndefinedVariable.empty()) {
        *errorText << "; because operands are neither both numeric nor both time";
        return;
    }

    *errorText << "; because undefined external variable(s): ";
    std::string separator;
    if (!leftUndefinedVariable.empty()) {
        *errorText << leftUndefinedVariable;
        separator = ", ";
    }
    if (!rightUndefinedVariable.empty()) {
        *errorText << separator << rightUndefinedVariable;
    }
}

bool toBool(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        return std::fabs(std::get<double>(runtimeValue)) > k_numeric_epsilon;
    }
    if (std::holds_alternative<std::string>(runtimeValue)) {
        const std::string loweredValue = toLower(std::get<std::string>(runtimeValue));
        if (loweredValue == "" || loweredValue == "false" || loweredValue == "off" || loweredValue == "0") {
            return false;
        }
        if (loweredValue == "true" || loweredValue == "on" || loweredValue == "1") {
            return true;
        }
        return true;
    }
    return false;
}

bool evalEquals(const RuntimeValue& leftValue, const RuntimeValue& rightValue) {
    if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
        return std::fabs(std::get<double>(leftValue) - std::get<double>(rightValue)) < k_numeric_epsilon;
    }

    const auto leftTime = tryTimeOfDay(leftValue);
    const auto rightTime = tryTimeOfDay(rightValue);
    if (leftTime.has_value() && rightTime.has_value()) {
        return *leftTime == *rightTime;
    }

    return valueToString(leftValue) == valueToString(rightValue);
}

} // namespace yaha
