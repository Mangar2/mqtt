#include "yaha/automation_client_rule_runtime/rule_event_gate.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "yaha/automation_client_rule_runtime/rule_field_access.h"
#include "yaha/automation_client_rule_runtime/rule_topic_filter.h"

namespace yaha {
namespace {

constexpr std::int64_t k_related_motion_window_seconds{5};
constexpr std::int64_t k_motion_stale_threshold_seconds{60};

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> findLatestMotionTimestampForTopic(
    const std::vector<MotionEventRecord>& recentMotionEvents,
    const std::string& topicName) {
    std::optional<std::chrono::system_clock::time_point> latestTimestamp;
    for (const auto& motionEvent : recentMotionEvents) {
        if (motionEvent.topicName != topicName) {
            continue;
        }
        if (!latestTimestamp.has_value() || motionEvent.timestamp > *latestTimestamp) {
            latestTimestamp = motionEvent.timestamp;
        }
    }
    return latestTimestamp;
}

[[nodiscard]] std::string formatTimeOfDay(const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    if (localtime_s(&localCalendarTime, &epochSeconds) != 0) {
        return {};
    }
#else
    if (localtime_r(&epochSeconds, &localCalendarTime) == nullptr) {
        return {};
    }
#endif

    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(2) << localCalendarTime.tm_hour
           << ':' << std::setw(2) << localCalendarTime.tm_min
           << ':' << std::setw(2) << localCalendarTime.tm_sec;
    return stream.str();
}

} // namespace

bool anyEventMatches(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters) {
    if (topicFilters.empty()) {
        return false;
    }

    return std::ranges::any_of(topicFilters, [&recentEventTopics](const std::string& topicFilter) {
        return std::ranges::any_of(recentEventTopics, [&topicFilter](const std::string& eventTopic) {
            return runtimeMatchesTopicFilter(topicFilter, eventTopic);
        });
    });
}

bool allFiltersMatchAnyEvent(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters) {
    if (topicFilters.empty()) {
        return true;
    }

    return std::ranges::all_of(topicFilters, [&recentEventTopics](const std::string& topicFilter) {
        return std::ranges::any_of(recentEventTopics, [&topicFilter](const std::string& eventTopic) {
            return runtimeMatchesTopicFilter(topicFilter, eventTopic);
        });
    });
}

std::set<std::string> collectRecentEventTopics(
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

std::set<std::string> collectRecentMotionTopics(
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

std::vector<MotionEventRecord> collectRecentMotionEvents(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    const bool inactivityGateConfigured) {
    std::vector<MotionEventRecord> motionEvents{};

    std::optional<std::chrono::system_clock::time_point> latestMotionTime;
    for (const auto& motionEvent : eventState.motionEvents) {
        if (!latestMotionTime.has_value() || motionEvent.timestamp > *latestMotionTime) {
            latestMotionTime = motionEvent.timestamp;
        }
    }

    if (!latestMotionTime.has_value()) {
        return motionEvents;
    }

    if (!inactivityGateConfigured
        && std::chrono::duration_cast<std::chrono::seconds>(evaluationTime - *latestMotionTime).count()
            > k_motion_stale_threshold_seconds) {
        return motionEvents;
    }

    for (const auto& motionEvent : eventState.motionEvents) {
        const auto deltaSeconds = std::chrono::duration_cast<std::chrono::seconds>(
            *latestMotionTime - motionEvent.timestamp);
        if (deltaSeconds.count() <= k_related_motion_window_seconds) {
            motionEvents.push_back(motionEvent);
        }
    }

    return motionEvents;
}

std::optional<std::chrono::system_clock::time_point> findLatestMotionTimestamp(
    const RuleRuntimeEventState& eventState) {
    std::optional<std::chrono::system_clock::time_point> latestMotionTime;
    for (const auto& motionEvent : eventState.motionEvents) {
        if (!latestMotionTime.has_value() || motionEvent.timestamp > *latestMotionTime) {
            latestMotionTime = motionEvent.timestamp;
        }
    }
    return latestMotionTime;
}

std::vector<std::string> collectMatchingTopics(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters) {
    std::vector<std::string> matchedTopics{};
    for (const auto& eventTopic : recentEventTopics) {
        const bool matchesFilter = std::ranges::any_of(topicFilters, [&eventTopic](const std::string& topicFilter) {
            return runtimeMatchesTopicFilter(topicFilter, eventTopic);
        });
        if (matchesFilter) {
            matchedTopics.push_back(eventTopic);
        }
    }
    return matchedTopics;
}

std::string joinText(const std::vector<std::string>& parts, const std::string_view separator) {
    std::string text{};
    for (std::size_t index = 0U; index < parts.size(); ++index) {
        if (index > 0U) {
            text.append(separator);
        }
        text.append(parts[index]);
    }
    return text;
}

std::vector<std::string> formatMatchedTopicsWithOptionalTimestamp(
    const std::vector<std::string>& matchedTopics,
    const std::vector<MotionEventRecord>& recentMotionEvents) {
    std::vector<std::string> formattedTopics{};
    formattedTopics.reserve(matchedTopics.size());
    for (const auto& topicName : matchedTopics) {
        std::string topicText = topicName;
        const auto latestMotionTimestamp = findLatestMotionTimestampForTopic(recentMotionEvents, topicName);
        if (latestMotionTimestamp.has_value()) {
            const std::string clockText = formatTimeOfDay(*latestMotionTimestamp);
            if (!clockText.empty()) {
                topicText += " (" + clockText + ")";
            }
        }
        formattedTopics.push_back(std::move(topicText));
    }
    return formattedTopics;
}

std::optional<std::string> buildEventTriggerReason(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime) {
    const bool hasAnyOfField = ruleObject.contains("anyOf");
    const bool hasAllOfField = ruleObject.contains("allOf");
    if (!hasAnyOfField && !hasAllOfField) {
        return std::nullopt;
    }

    const auto inactivityMinutes = readNumberField(ruleObject, "durationWithoutMovementInMinutes");
    const bool hasInactivityGate = inactivityMinutes.has_value();

    const std::set<std::string> recentEventTopics = collectRecentEventTopics(
        eventState, evaluationTime, hasInactivityGate);
    const std::vector<MotionEventRecord> recentMotionEvents = collectRecentMotionEvents(
        eventState, evaluationTime, hasInactivityGate);

    const std::vector<std::string> anyOfFilters = readTopicFilterList(ruleObject, "anyOf");
    const std::vector<std::string> allOfFilters = readTopicFilterList(ruleObject, "allOf");

    const bool allOfMatch = hasAllOfField && allFiltersMatchAnyEvent(recentEventTopics, allOfFilters);
    const bool anyOfMatch = hasAnyOfField && anyEventMatches(recentEventTopics, anyOfFilters);

    std::vector<std::string> gateDescriptions{};
    if (allOfMatch) {
        const std::vector<std::string> matchedTopics = collectMatchingTopics(recentEventTopics, allOfFilters);
        const std::vector<std::string> formattedTopics = formatMatchedTopicsWithOptionalTimestamp(
            matchedTopics,
            recentMotionEvents);
        if (!formattedTopics.empty()) {
            gateDescriptions.push_back("allOf: " + joinText(formattedTopics, ", "));
        }
    }

    if (anyOfMatch) {
        const std::vector<std::string> matchedTopics = collectMatchingTopics(recentEventTopics, anyOfFilters);
        const std::vector<std::string> formattedTopics = formatMatchedTopicsWithOptionalTimestamp(
            matchedTopics,
            recentMotionEvents);
        if (!formattedTopics.empty()) {
            gateDescriptions.push_back("anyOf: " + joinText(formattedTopics, ", "));
        }
    }

    if (gateDescriptions.empty()) {
        return std::nullopt;
    }

    return std::string{"Events: "} + joinText(gateDescriptions, "; ");
}

bool evaluateEventGates(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime) {
    const auto inactivityMinutes = readNumberField(ruleObject, "durationWithoutMovementInMinutes");
    const bool hasInactivityGate = inactivityMinutes.has_value();

    if (hasInactivityGate) {
        const auto latestMotionTime = findLatestMotionTimestamp(eventState);
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

    const auto allowFiltersOpt = readTopicFilterArrayOnly(ruleObject, "allow");
    if (!allowFiltersOpt.has_value()) {
        return true;
    }

    std::vector<std::string> allowedFilters = *allowFiltersOpt;
    allowedFilters.insert(allowedFilters.end(), allOfFilters.begin(), allOfFilters.end());
    allowedFilters.insert(allowedFilters.end(), anyOfFilters.begin(), anyOfFilters.end());

    for (const auto& motionTopic : recentMotionTopics) {
        const bool matchesAllowSet = std::ranges::any_of(allowedFilters, [&motionTopic](const std::string& filter) {
            return runtimeMatchesTopicFilter(filter, motionTopic);
        });
        if (!matchesAllowSet) {
            return false;
        }
    }

    return true;
}

} // namespace yaha
