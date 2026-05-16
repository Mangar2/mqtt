#include "yaha/automation_client/rule_runtime_engine.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <ctime>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "yaha/automation/expression_parser.h"
#include "yaha/automation/single_rule_processor.h"

namespace yaha {
namespace {

constexpr std::size_t k_max_motion_events{100U};
constexpr std::size_t k_motion_trim_count{20U};
constexpr std::int64_t k_related_motion_window_seconds{5};
constexpr std::int64_t k_motion_stale_threshold_seconds{60};
constexpr std::int64_t k_default_duration_seconds{6 * 60 * 60};
constexpr std::int64_t k_seconds_per_day{24 * 60 * 60};
constexpr int k_minutes_per_hour{60};
constexpr int k_hours_per_day{24};
constexpr double k_zero_epsilon{1e-12};

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

[[nodiscard]] std::string joinPath(const std::string& basePath, const std::string& segment) {
    if (basePath.empty()) {
        return segment;
    }
    return basePath + "/" + segment;
}

[[nodiscard]] bool isRuleNode(const RuleTreeNode& node) {
    return node.isObject() && node.asObject().contains("topic");
}

[[nodiscard]] bool topicShapeValid(const RuleTreeNode& topicNode) {
    if (topicNode.isString()) {
        return !topicNode.asString().empty();
    }

    if (topicNode.isArray()) {
        const auto& topicArray = topicNode.asArray();
        if (topicArray.empty()) {
            return false;
        }
        return std::ranges::all_of(topicArray, [](const RuleTreeNode& entryNode) {
            return entryNode.isString() && !entryNode.asString().empty();
        });
    }

    if (topicNode.isObject()) {
        const auto& topicMap = topicNode.asObject();
        if (topicMap.empty()) {
            return false;
        }
        return std::ranges::all_of(topicMap, [](const auto& mapEntry) {
            return !mapEntry.first.empty();
        });
    }

    return false;
}

[[nodiscard]] std::string valueToStableText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return "s:" + std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return "n:" + textStream.str();
}

[[nodiscard]] bool isZeroPayloadValue(const Value& messageValue) {
    if (std::holds_alternative<double>(messageValue)) {
        return std::fabs(std::get<double>(messageValue)) <= k_zero_epsilon;
    }

    const auto& valueText = std::get<std::string>(messageValue);
    return valueText == "0";
}

[[nodiscard]] bool matchesTopicFilter(const std::string_view topicFilter, const std::string_view topicName) {
    std::size_t filterPos = 0U;
    std::size_t topicPos = 0U;

    while (filterPos < topicFilter.size()) {
        if (topicPos > topicName.size()) {
            return false;
        }

        const std::size_t filterEnd = topicFilter.find('/', filterPos);
        const std::size_t topicEnd = topicName.find('/', topicPos);

        const std::string_view filterSegment = filterEnd == std::string_view::npos
            ? topicFilter.substr(filterPos)
            : topicFilter.substr(filterPos, filterEnd - filterPos);
        std::string_view topicSegment{};
        if (topicPos <= topicName.size()) {
            if (topicEnd == std::string_view::npos) {
                topicSegment = topicName.substr(topicPos);
            } else {
                topicSegment = topicName.substr(topicPos, topicEnd - topicPos);
            }
        }

        if (filterSegment == "#") {
            return true;
        }

        if (topicPos >= topicName.size()) {
            return false;
        }

        if (filterSegment != "+" && filterSegment != topicSegment) {
            return false;
        }

        if (filterEnd == std::string_view::npos) {
            return topicEnd == std::string_view::npos;
        }

        if (topicEnd == std::string_view::npos) {
            return false;
        }

        filterPos = filterEnd + 1U;
        topicPos = topicEnd + 1U;
    }

    return topicPos == topicName.size();
}

[[nodiscard]] bool isMotionTopic(
    const std::string& topicName,
    const std::vector<std::string>& motionTopicFilters) {
    return std::ranges::any_of(motionTopicFilters, [&topicName](const std::string& topicFilter) {
        return matchesTopicFilter(topicFilter, topicName);
    });
}

[[nodiscard]] std::optional<double> readNumberField(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end()) {
        return std::nullopt;
    }

    if (!std::holds_alternative<double>(fieldIter->second.value)) {
        return std::nullopt;
    }

    return std::get<double>(fieldIter->second.value);
}

[[nodiscard]] std::vector<std::string> readTopicFilterList(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    std::vector<std::string> filters{};
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end()) {
        return filters;
    }

    if (fieldIter->second.isString()) {
        filters.push_back(fieldIter->second.asString());
        return filters;
    }

    if (!fieldIter->second.isArray()) {
        return filters;
    }

    for (const auto& arrayNode : fieldIter->second.asArray()) {
        if (!arrayNode.isString()) {
            return {};
        }
        filters.push_back(arrayNode.asString());
    }

    return filters;
}

[[nodiscard]] std::optional<std::vector<std::string>> readTopicFilterArrayOnly(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end() || !fieldIter->second.isArray()) {
        return std::nullopt;
    }

    std::vector<std::string> filters{};
    for (const auto& arrayNode : fieldIter->second.asArray()) {
        if (!arrayNode.isString()) {
            return std::nullopt;
        }
        filters.push_back(arrayNode.asString());
    }
    return filters;
}

[[nodiscard]] bool fieldIsArray(const RuleTreeNode::Object& ruleObject, const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    return fieldIter != ruleObject.end() && fieldIter->second.isArray();
}

[[nodiscard]] std::string asciiLower(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return textValue;
}

[[nodiscard]] bool anyEventMatches(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters) {
    if (topicFilters.empty()) {
        return false;
    }

    return std::ranges::any_of(topicFilters, [&recentEventTopics](const std::string& topicFilter) {
        return std::ranges::any_of(recentEventTopics, [&topicFilter](const std::string& eventTopic) {
            return matchesTopicFilter(topicFilter, eventTopic);
        });
    });
}

[[nodiscard]] bool allFiltersMatchAnyEvent(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters) {
    if (topicFilters.empty()) {
        return true;
    }

    return std::ranges::all_of(topicFilters, [&recentEventTopics](const std::string& topicFilter) {
        return std::ranges::any_of(recentEventTopics, [&topicFilter](const std::string& eventTopic) {
            return matchesTopicFilter(topicFilter, eventTopic);
        });
    });
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

[[nodiscard]] bool readActiveFlag(const RuleTreeNode::Object& ruleObject) {
    const auto activeIter = ruleObject.find("active");
    if (activeIter == ruleObject.end()) {
        return true;
    }

    if (!std::holds_alternative<bool>(activeIter->second.value)) {
        return true;
    }

    return std::get<bool>(activeIter->second.value);
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

[[nodiscard]] bool evaluateWeekdayGate(
    const RuleTreeNode::Object& ruleObject,
    const std::chrono::system_clock::time_point& evaluationTime) {
    const auto weekdays = readWeekdays(ruleObject);
    if (!weekdays.has_value() || weekdays->empty()) {
        return true;
    }

    const int weekdayValue = weekdayFromTimePoint(evaluationTime);
    return weekdays->contains(weekdayValue);
}

[[nodiscard]] bool evaluateTimeWindowGate(
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

[[nodiscard]] std::set<std::string> collectRecentEventTopics(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    const bool inactivityGateConfigured) {
    std::set<std::string> eventTopics = eventState.nonMotionEvents;

    std::optional<std::chrono::system_clock::time_point> latestMotionTime;
    for (const auto& motionEvent : eventState.motionEvents) {
        if (!latestMotionTime.has_value() || motionEvent.timestamp > *latestMotionTime) {
            latestMotionTime = motionEvent.timestamp;
        }
    }

    if (!latestMotionTime.has_value()) {
        return eventTopics;
    }

    if (!inactivityGateConfigured
        && std::chrono::duration_cast<std::chrono::seconds>(evaluationTime - *latestMotionTime).count()
            > k_motion_stale_threshold_seconds) {
        return eventTopics;
    }

    for (const auto& motionEvent : eventState.motionEvents) {
        const auto deltaSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            *latestMotionTime - motionEvent.timestamp);
        if (deltaSeconds.count() <= k_related_motion_window_seconds) {
            eventTopics.insert(motionEvent.topicName);
        }
    }

    return eventTopics;
}

[[nodiscard]] std::set<std::string> collectRecentMotionTopics(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    const bool inactivityGateConfigured) {
    std::set<std::string> motionTopics{};

    std::optional<std::chrono::system_clock::time_point> latestMotionTime;
    for (const auto& motionEvent : eventState.motionEvents) {
        if (!latestMotionTime.has_value() || motionEvent.timestamp > *latestMotionTime) {
            latestMotionTime = motionEvent.timestamp;
        }
    }

    if (!latestMotionTime.has_value()) {
        return motionTopics;
    }

    if (!inactivityGateConfigured
        && std::chrono::duration_cast<std::chrono::seconds>(evaluationTime - *latestMotionTime).count()
            > k_motion_stale_threshold_seconds) {
        return motionTopics;
    }

    for (const auto& motionEvent : eventState.motionEvents) {
        const auto deltaSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            *latestMotionTime - motionEvent.timestamp);
        if (deltaSeconds.count() <= k_related_motion_window_seconds) {
            motionTopics.insert(motionEvent.topicName);
        }
    }

    return motionTopics;
}

[[nodiscard]] bool evaluateEventGates(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime) {
    const auto inactivityMinutes = readNumberField(ruleObject, "durationWithoutMovementInMinutes");
    const bool hasInactivityGate = inactivityMinutes.has_value();

    if (hasInactivityGate) {
        std::optional<std::chrono::system_clock::time_point> latestMotionTime;
        for (const auto& motionEvent : eventState.motionEvents) {
            if (!latestMotionTime.has_value() || motionEvent.timestamp > *latestMotionTime) {
                latestMotionTime = motionEvent.timestamp;
            }
        }

        if (latestMotionTime.has_value()) {
            const auto elapsedMinutes = std::chrono::duration_cast<std::chrono::minutes>(
                evaluationTime - *latestMotionTime);
            if (elapsedMinutes.count() < static_cast<long long>(*inactivityMinutes)) {
                return false;
            }
        }
    }

    const std::set<std::string> recentEventTopics = collectRecentEventTopics(
        eventState, evaluationTime, hasInactivityGate);
    const std::set<std::string> recentMotionTopics = collectRecentMotionTopics(
        eventState, evaluationTime, hasInactivityGate);

    // Legacy JS semantics: (allOf OR anyOf) AND !noneOf.
    const bool hasAnyOfField = ruleObject.contains("anyOf");
    const bool hasAllOfField = ruleObject.contains("allOf");
    if (!hasAnyOfField && !hasAllOfField) {
        return true;
    }

    const std::vector<std::string> anyOfFilters = readTopicFilterList(ruleObject, "anyOf");
    const std::vector<std::string> allOfFilters = readTopicFilterList(ruleObject, "allOf");
    const bool allOfMatch = hasAllOfField && allFiltersMatchAnyEvent(recentEventTopics, allOfFilters);
    const bool anyOfMatch = hasAnyOfField && anyEventMatches(recentEventTopics, anyOfFilters);
    if (!(allOfMatch || anyOfMatch)) {
        return false;
    }

    const std::vector<std::string> noneOfFilters = readTopicFilterList(ruleObject, "noneOf");
    if (!noneOfFilters.empty() && anyEventMatches(recentEventTopics, noneOfFilters)) {
        return false;
    }

    // Legacy JS semantics: apply allow only when allow is an array and require
    // all recent motion topics to be within (allow + allOf + anyOf).
    const auto allowFiltersOpt = readTopicFilterArrayOnly(ruleObject, "allow");
    if (!allowFiltersOpt.has_value()) {
        return true;
    }

    std::vector<std::string> allowedFilters = *allowFiltersOpt;
    allowedFilters.insert(allowedFilters.end(), allOfFilters.begin(), allOfFilters.end());
    allowedFilters.insert(allowedFilters.end(), anyOfFilters.begin(), anyOfFilters.end());

    for (const auto& motionTopic : recentMotionTopics) {
        const bool matchesAllowSet = std::ranges::any_of(allowedFilters, [&motionTopic](const std::string& filter) {
            return matchesTopicFilter(filter, motionTopic);
        });
        if (!matchesAllowSet) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::optional<double> readPositiveGateSeconds(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto value = readNumberField(ruleObject, fieldName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (*value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::vector<Message> applyDeliveryControls(
    const std::string& rulePath,
    const RuleTreeNode::Object& ruleObject,
    const std::vector<Message>& candidateMessages,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeDeliveryState* deliveryState) {
    std::vector<Message> emittedMessages{};
    emittedMessages.reserve(candidateMessages.size());

    const auto delaySeconds = readPositiveGateSeconds(ruleObject, "delayInSeconds");
    const auto cooldownSeconds = readPositiveGateSeconds(ruleObject, "cooldownInSeconds");
    const bool eventTriggeredRule = fieldIsArray(ruleObject, "anyOf") || fieldIsArray(ruleObject, "allOf");

    for (const auto& candidateMessage : candidateMessages) {
        const std::string outputKey = rulePath + "|" + candidateMessage.topic();
        const std::string candidateHash = candidateMessage.topic() + "|" + valueToStableText(candidateMessage.value());

        auto& outputState = deliveryState->outputStatesByKey[outputKey];
        if (outputState.candidateHash != candidateHash) {
            outputState.candidateHash = candidateHash;
            outputState.candidateSince = evaluationTime;
        }

        if (delaySeconds.has_value()) {
            const auto stableFor = std::chrono::duration_cast<std::chrono::duration<double>>(
                evaluationTime - outputState.candidateSince);
            if (stableFor.count() < *delaySeconds) {
                continue;
            }
        }

        if (!outputState.emittedHash.has_value() || *outputState.emittedHash != candidateHash) {
            outputState.emittedHash = candidateHash;
            outputState.emittedAt = evaluationTime;
            emittedMessages.push_back(candidateMessage.clone());
            continue;
        }

        if (!cooldownSeconds.has_value() && eventTriggeredRule) {
            outputState.emittedAt = evaluationTime;
            emittedMessages.push_back(candidateMessage.clone());
            continue;
        }

        if (cooldownSeconds.has_value() && outputState.emittedAt.has_value()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                evaluationTime - *outputState.emittedAt);
            if (elapsed.count() >= *cooldownSeconds) {
                outputState.emittedAt = evaluationTime;
                emittedMessages.push_back(candidateMessage.clone());
            }
        }
    }

    return emittedMessages;
}

[[nodiscard]] bool shouldRetainDeliveryStateOnGateMiss(const RuleTreeNode::Object& ruleObject) {
    return readNumberField(ruleObject, "cooldownInSeconds").has_value();
}

void clearRuleDeliveryState(const std::string& rulePath, RuleRuntimeDeliveryState* deliveryState) {
    std::vector<std::string> keysToErase{};
    keysToErase.reserve(deliveryState->outputStatesByKey.size());

    const std::string keyPrefix = rulePath + "|";
    for (const auto& [stateKey, stateValue] : deliveryState->outputStatesByKey) {
        (void)stateValue;
        if (stateKey.starts_with(keyPrefix)) {
            keysToErase.push_back(stateKey);
        }
    }

    for (const auto& stateKey : keysToErase) {
        deliveryState->outputStatesByKey.erase(stateKey);
    }
}

void appendPathError(
    RuleRuntimeProcessingResult* result,
    const std::string& pathText,
    const std::string& errorText) {
    result->success = false;
    std::string formattedError = pathText;
    formattedError.append(": ");
    formattedError.append(errorText);
    result->errors.push_back(std::move(formattedError));
}

void processRuleNode(
    const RuleTreeNode& node,
    const std::string& pathText,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState,
    RuleRuntimeProcessingResult* result) {
    result->processedRules += 1U;
    const auto& ruleObject = node.asObject();
    const bool retainDeliveryStateOnGateMiss = shouldRetainDeliveryStateOnGateMiss(ruleObject);

    if (!readActiveFlag(ruleObject) || !evaluateWeekdayGate(ruleObject, evaluationTime)) {
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return;
    }

    std::vector<std::string> gateErrors{};
    const bool timeGatePass = evaluateTimeWindowGate(ruleObject, variables, evaluationTime, &gateErrors);
    if (!gateErrors.empty()) {
        for (const auto& errorText : gateErrors) {
            appendPathError(result, pathText, errorText);
        }
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return;
    }

    if (!timeGatePass || !evaluateEventGates(ruleObject, *eventState, evaluationTime)) {
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return;
    }

    const SingleRuleProcessingResult singleResult = SingleRuleProcessor::process(
        node,
        variables,
        pathText);
    result->usedVariables.insert(singleResult.usedVariables.begin(), singleResult.usedVariables.end());

    if (!singleResult.success) {
        for (const auto& errorText : singleResult.errors) {
            appendPathError(result, pathText, errorText);
        }
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return;
    }

    if (!singleResult.triggered || singleResult.messages.empty()) {
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return;
    }

    result->triggeredRules += 1U;
    const std::vector<Message> emittedMessages = applyDeliveryControls(
        pathText, ruleObject, singleResult.messages, evaluationTime, deliveryState);
    for (const auto& emittedMessage : emittedMessages) {
        result->messages.push_back(emittedMessage.clone());
    }
}

void processRulesRecursively(
    const RuleTreeNode& node,
    const std::string& pathText,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState,
    RuleRuntimeProcessingResult* result) {
    if (isRuleNode(node)) {
        processRuleNode(
            node,
            pathText,
            variables,
            evaluationTime,
            eventState,
            deliveryState,
            result);
    }

    if (node.isObject()) {
        for (const auto& [fieldName, childNode] : node.asObject()) {
            processRulesRecursively(
                childNode,
                joinPath(pathText, fieldName),
                variables,
                evaluationTime,
                eventState,
                deliveryState,
                result);
        }
        return;
    }

    if (node.isArray()) {
        const auto& nodeArray = node.asArray();
        for (std::size_t indexValue = 0U; indexValue < nodeArray.size(); ++indexValue) {
            processRulesRecursively(
                nodeArray[indexValue],
                joinPath(pathText, std::to_string(indexValue)),
                variables,
                evaluationTime,
                eventState,
                deliveryState,
                result);
        }
    }
}

} // namespace

bool RuleRuntimeEngine::isRuleNodeStructureValid(const RuleTreeNode& ruleNode) {
    if (!ruleNode.isObject()) {
        return false;
    }

    const auto& ruleObject = ruleNode.asObject();
    const auto topicIter = ruleObject.find("topic");
    if (topicIter == ruleObject.end()) {
        return false;
    }

    return topicShapeValid(topicIter->second);
}

void RuleRuntimeEngine::ingestDomainMessageEvent(
    const Message& message,
    const std::chrono::system_clock::time_point& messageTime,
    const std::vector<std::string>& motionTopicFilters,
    RuleRuntimeEventState* eventState) {
    if (isZeroPayloadValue(message.value())) {
        return;
    }

    if (isMotionTopic(message.topic(), motionTopicFilters)) {
        eventState->motionEvents.push_back(MotionEventRecord{
            .topicName = message.topic(),
            .timestamp = messageTime,
            .sequenceNumber = eventState->nextSequenceNumber,
        });
        eventState->nextSequenceNumber += 1U;

        if (eventState->motionEvents.size() > k_max_motion_events) {
            const std::size_t eraseCount = std::min(k_motion_trim_count, eventState->motionEvents.size());
            eventState->motionEvents.erase(
                eventState->motionEvents.begin(),
                eventState->motionEvents.begin() + static_cast<std::ptrdiff_t>(eraseCount));
        }
        return;
    }

    eventState->nonMotionEvents.insert(message.topic());
}

RuleRuntimeProcessingResult RuleRuntimeEngine::processRules(
    const RuleTreeNode& rulesRoot,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState) {
    RuleRuntimeProcessingResult result{};
    processRulesRecursively(
        rulesRoot,
        std::string{},
        variables,
        evaluationTime,
        eventState,
        deliveryState,
        &result);
    return result;
}

std::vector<Message> RuleRuntimeEngine::previewDeliveredMessages(
    const std::string& rulePath,
    const RuleTreeNode& ruleNode,
    const std::vector<Message>& candidateMessages,
    const std::chrono::system_clock::time_point& evaluationTime,
    const RuleRuntimeDeliveryState& deliveryState) {
    if (!ruleNode.isObject()) {
        return {};
    }

    RuleRuntimeDeliveryState deliveryStateCopy = deliveryState;
    return applyDeliveryControls(
        rulePath,
        ruleNode.asObject(),
        candidateMessages,
        evaluationTime,
        &deliveryStateCopy);
}

void RuleRuntimeEngine::clearNonMotionEvents(RuleRuntimeEventState* eventState) {
    eventState->nonMotionEvents.clear();
}

} // namespace yaha
