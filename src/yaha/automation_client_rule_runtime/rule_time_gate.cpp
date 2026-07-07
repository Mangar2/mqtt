#include "yaha/automation_client_rule_runtime/rule_time_gate.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <ctime>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>

#include "yaha/automation/expression_parser.h"

namespace yaha {
namespace {

constexpr std::int64_t k_default_duration_seconds{6 * 60 * 60};
constexpr std::int64_t k_seconds_per_day{24 * 60 * 60};
constexpr int k_minutes_per_hour{60};
constexpr int k_hours_per_day{24};

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

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> localDayStart(
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

[[nodiscard]] std::string asciiLower(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return textValue;
}

[[nodiscard]] std::optional<std::chrono::seconds> parseDurationText(const std::string& durationText) {
    const std::size_t separatorPos = durationText.find(':');
    if (separatorPos == std::string::npos || separatorPos == 0U || separatorPos + 1U >= durationText.size()) {
        return std::nullopt;
    }

    const std::string hoursText = durationText.substr(0U, separatorPos);
    const std::string minutesText = durationText.substr(separatorPos + 1U);

    int hoursValue = 0;
    int minutesValue = 0;

    const auto hoursResult = std::from_chars(
        hoursText.data(), hoursText.data() + static_cast<std::ptrdiff_t>(hoursText.size()), hoursValue);
    const auto minutesResult = std::from_chars(
        minutesText.data(), minutesText.data() + static_cast<std::ptrdiff_t>(minutesText.size()), minutesValue);

    if (hoursResult.ec != std::errc{} || minutesResult.ec != std::errc{}) {
        return std::nullopt;
    }

    if (hoursValue < 0 || minutesValue < 0 || minutesValue >= k_minutes_per_hour) {
        return std::nullopt;
    }

    return std::chrono::hours{hoursValue} + std::chrono::minutes{minutesValue};
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> parseTimeOfDay(
    const std::string& textValue,
    const std::chrono::system_clock::time_point& referenceDate) {
    const std::size_t firstSeparator = textValue.find(':');
    if (firstSeparator == std::string::npos || firstSeparator == 0U || firstSeparator + 1U >= textValue.size()) {
        return std::nullopt;
    }

    const std::size_t secondSeparator = textValue.find(':', firstSeparator + 1U);
    const std::string hoursText = textValue.substr(0U, firstSeparator);
    const std::string minutesText = secondSeparator == std::string::npos
        ? textValue.substr(firstSeparator + 1U)
        : textValue.substr(firstSeparator + 1U, secondSeparator - firstSeparator - 1U);
    const std::string secondsText = secondSeparator == std::string::npos
        ? std::string{"0"}
        : textValue.substr(secondSeparator + 1U);

    int hoursValue = 0;
    int minutesValue = 0;
    int secondsValue = 0;

    const auto hoursResult = std::from_chars(hoursText.data(), hoursText.data() + hoursText.size(), hoursValue);
    const auto minutesResult = std::from_chars(minutesText.data(), minutesText.data() + minutesText.size(), minutesValue);
    const auto secondsResult = std::from_chars(secondsText.data(), secondsText.data() + secondsText.size(), secondsValue);

    if (hoursResult.ec != std::errc{} || minutesResult.ec != std::errc{} || secondsResult.ec != std::errc{}) {
        return std::nullopt;
    }

    if (hoursValue < 0
        || hoursValue >= k_hours_per_day
        || minutesValue < 0
        || minutesValue >= k_minutes_per_hour
        || secondsValue < 0
        || secondsValue >= k_minutes_per_hour) {
        return std::nullopt;
    }

    const auto dayPoint = localDayStart(referenceDate);
    if (!dayPoint.has_value()) {
        return std::nullopt;
    }

    return *dayPoint
        + std::chrono::hours{hoursValue}
        + std::chrono::minutes{minutesValue}
        + std::chrono::seconds{secondsValue};
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> evaluateRuleStartTime(
    const RuleTreeNode::Object& ruleObject,
    const ExpressionEvaluator::VariableMap& variables,
    std::vector<std::string>* errors) {
    const auto timeIter = ruleObject.find("time");
    if (timeIter == ruleObject.end()) {
        return std::nullopt;
    }

    if (!timeIter->second.isString()) {
        errors->emplace_back("time must be expression string");
        return std::nullopt;
    }

    const ExpressionParseResult parseResult = ExpressionParser::parse(timeIter->second.asString());
    if (!parseResult.success) {
        errors->emplace_back("time parse failed");
        return std::nullopt;
    }

    const ExpressionEvaluationResult evaluationResult = ExpressionEvaluator::evaluate(parseResult.ast, variables);
    if (!evaluationResult.success) {
        errors->emplace_back("time evaluation failed");
        return std::nullopt;
    }

    if (std::holds_alternative<std::chrono::system_clock::time_point>(evaluationResult.value)) {
        return std::get<std::chrono::system_clock::time_point>(evaluationResult.value);
    }

    if (std::holds_alternative<std::string>(evaluationResult.value)) {
        const auto nowIter = variables.find("/time");
        const auto nowPoint = nowIter != variables.end()
            && std::holds_alternative<std::chrono::system_clock::time_point>(nowIter->second)
            ? std::get<std::chrono::system_clock::time_point>(nowIter->second)
            : std::chrono::system_clock::now();
        const auto parsedTime = parseTimeOfDay(std::get<std::string>(evaluationResult.value), nowPoint);
        if (!parsedTime.has_value()) {
            errors->emplace_back("time expression must resolve to HH:MM[:SS] or time value");
        }
        return parsedTime;
    }

    errors->emplace_back("time expression produced unsupported value");
    return std::nullopt;
}

[[nodiscard]] std::chrono::seconds readDurationSeconds(const RuleTreeNode::Object& ruleObject) {
    const auto durationIter = ruleObject.find("duration");
    if (durationIter == ruleObject.end()) {
        return std::chrono::seconds{k_default_duration_seconds};
    }

    if (!durationIter->second.isString()) {
        return std::chrono::seconds{k_default_duration_seconds};
    }

    const auto parsedDuration = parseDurationText(durationIter->second.asString());
    if (!parsedDuration.has_value()) {
        return std::chrono::seconds{k_default_duration_seconds};
    }

    return *parsedDuration;
}

[[nodiscard]] std::optional<std::set<int>> readWeekdays(const RuleTreeNode::Object& ruleObject) {
    const auto weekdaysIter = ruleObject.find("weekdays");
    if (weekdaysIter == ruleObject.end()) {
        return std::nullopt;
    }

    if (!weekdaysIter->second.isArray()) {
        return std::nullopt;
    }

    static const std::array<std::pair<std::string_view, int>, 7U> k_day_map{{
        {"sun", 0}, {"mon", 1}, {"tue", 2}, {"wed", 3}, {"thu", 4}, {"fri", 5}, {"sat", 6}}};

    std::set<int> weekdaySet{};
    for (const auto& weekdayNode : weekdaysIter->second.asArray()) {
        if (!weekdayNode.isString()) {
            return std::nullopt;
        }

        const std::string weekdayText = asciiLower(weekdayNode.asString());
        const auto* const mapIter = std::ranges::find_if(k_day_map, [&weekdayText](const auto& dayEntry) {
            return dayEntry.first == weekdayText;
        });
        if (mapIter == k_day_map.end()) {
            return std::nullopt;
        }
        weekdaySet.insert(mapIter->second);
    }

    return weekdaySet;
}

[[nodiscard]] int weekdayFromTimePoint(const std::chrono::system_clock::time_point& timePoint) {
    const auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        const auto dayPoint = std::chrono::floor<std::chrono::days>(timePoint);
        const std::chrono::weekday weekdayValue{dayPoint};
        return static_cast<int>(weekdayValue.c_encoding());
    }
    return localCalendarTime->tm_wday;
}

} // namespace

bool evaluateWeekdayGate(
    const RuleTreeNode::Object& ruleObject,
    const std::chrono::system_clock::time_point& evaluationTime) {
    const auto weekdays = readWeekdays(ruleObject);
    if (!weekdays.has_value() || weekdays->empty()) {
        return true;
    }

    const int weekdayValue = weekdayFromTimePoint(evaluationTime);
    return weekdays->contains(weekdayValue);
}

bool evaluateTimeWindowGate(
    const RuleTreeNode::Object& ruleObject,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    std::vector<std::string>* errors) {
    const auto startTime = evaluateRuleStartTime(ruleObject, variables, errors);
    if (!ruleObject.contains("time")) {
        return true;
    }

    if (!startTime.has_value()) {
        return false;
    }

    const std::chrono::seconds duration = readDurationSeconds(ruleObject);
    const auto endTime = *startTime + duration;

    if (evaluationTime >= *startTime && evaluationTime <= endTime) {
        return true;
    }

    const auto previousDayStart = *startTime - std::chrono::seconds{k_seconds_per_day};
    const auto previousDayEnd = previousDayStart + duration;
    return evaluationTime >= previousDayStart && evaluationTime <= previousDayEnd;
}

} // namespace yaha
