#include "yaha/automation_client_rule_runtime/rule_event_gate_trace.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yaha/automation_client_rule_runtime/rule_event_gate.h"
#include "yaha/automation_client_rule_runtime/rule_field_access.h"
#include "yaha/automation_client_rule_runtime/rule_topic_filter.h"

namespace yaha {
namespace {

struct EventGateTraceContext {
    std::vector<std::string> anyOfFilters;
    std::vector<std::string> allOfFilters;
    std::vector<std::string> noneOfFilters;
    std::optional<std::vector<std::string>> allowFilters;
    std::set<std::string> recentEventTopics;
    std::set<std::string> recentMotionTopics;
    std::vector<MotionEventRecord> recentMotionEvents;
};

void appendInactivityTraceEntry(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    const std::optional<double>& inactivityMinutes,
    std::vector<std::string>* traceEntries) {
    if (!inactivityMinutes.has_value()) {
        return;
    }

    const auto latestMotionTime = findLatestMotionTimestamp(eventState);
    if (!latestMotionTime.has_value()) {
        appendRuntimeTrace(
            traceEntries,
            "durationWithoutMovementInMinutes: passed (no recent motion event present)");
        return;
    }

    const auto elapsedMinutes = std::chrono::duration_cast<std::chrono::minutes>(
        evaluationTime - *latestMotionTime);
    const bool inactivityPassed = elapsedMinutes.count() >= static_cast<long long>(*inactivityMinutes);
    appendRuntimeTrace(
        traceEntries,
        "durationWithoutMovementInMinutes: "
            + std::string{inactivityPassed ? "passed" : "failed"}
            + " (elapsed=" + std::to_string(elapsedMinutes.count())
            + " min, required=" + std::to_string(static_cast<long long>(*inactivityMinutes)) + " min)");
}

[[nodiscard]] EventGateTraceContext buildEventGateTraceContext(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    const bool inactivityConfigured) {
    EventGateTraceContext context{};
    context.anyOfFilters = readTopicFilterList(ruleObject, "anyOf");
    context.allOfFilters = readTopicFilterList(ruleObject, "allOf");
    context.noneOfFilters = readTopicFilterList(ruleObject, "noneOf");
    context.allowFilters = readTopicFilterArrayOnly(ruleObject, "allow");
    context.recentEventTopics = collectRecentEventTopics(eventState, evaluationTime, inactivityConfigured);
    context.recentMotionTopics = collectRecentMotionTopics(eventState, evaluationTime, inactivityConfigured);
    context.recentMotionEvents = collectRecentMotionEvents(eventState, evaluationTime, inactivityConfigured);
    return context;
}

void appendAllOfTraceEntry(
    const EventGateTraceContext& context,
    const bool hasAllOfField,
    std::vector<std::string>* traceEntries) {
    if (!hasAllOfField) {
        return;
    }

    const bool allOfPassed = allFiltersMatchAnyEvent(context.recentEventTopics, context.allOfFilters);
    const std::vector<std::string> matchedTopics = collectMatchingTopics(
        context.recentEventTopics,
        context.allOfFilters);
    const std::vector<std::string> formattedTopics = formatMatchedTopicsWithOptionalTimestamp(
        matchedTopics,
        context.recentMotionEvents);
    if (allOfPassed) {
        if (formattedTopics.empty()) {
            appendRuntimeTrace(traceEntries, "allOf: passed (all configured filters matched)");
        } else {
            appendRuntimeTrace(
                traceEntries,
                "allOf: passed (events " + joinText(formattedTopics, ", ") + ")");
        }
        return;
    }

    appendRuntimeTrace(
        traceEntries,
        "allOf: failed (required events missing)");
}

void appendAnyOfTraceEntry(
    const EventGateTraceContext& context,
    const bool hasAnyOfField,
    std::vector<std::string>* traceEntries) {
    if (!hasAnyOfField) {
        return;
    }

    const bool anyOfPassed = anyEventMatches(context.recentEventTopics, context.anyOfFilters);
    const std::vector<std::string> matchedTopics = collectMatchingTopics(
        context.recentEventTopics,
        context.anyOfFilters);
    const std::vector<std::string> formattedTopics = formatMatchedTopicsWithOptionalTimestamp(
        matchedTopics,
        context.recentMotionEvents);
    if (anyOfPassed) {
        if (formattedTopics.empty()) {
            appendRuntimeTrace(traceEntries, "anyOf: passed (matching event found)");
        } else {
            appendRuntimeTrace(
                traceEntries,
                "anyOf: passed (event " + joinText(formattedTopics, ", ") + ")");
        }
        return;
    }

    appendRuntimeTrace(
        traceEntries,
        "anyOf: failed (no matching event)");
}

void appendNoneOfTraceEntry(
    const EventGateTraceContext& context,
    const bool hasNoneOfField,
    std::vector<std::string>* traceEntries) {
    if (!hasNoneOfField) {
        return;
    }

    const std::vector<std::string> matchedTopics = collectMatchingTopics(
        context.recentEventTopics,
        context.noneOfFilters);
    if (matchedTopics.empty()) {
        appendRuntimeTrace(traceEntries, "noneOf: passed (no blocked event matched)");
        return;
    }

    appendRuntimeTrace(
        traceEntries,
        "noneOf: failed (blocked event " + joinText(matchedTopics, ", ") + ")");
}

void appendAllowTraceEntry(
    const EventGateTraceContext& context,
    std::vector<std::string>* traceEntries) {
    if (!context.allowFilters.has_value()) {
        return;
    }

    std::vector<std::string> allowedFilters = *context.allowFilters;
    allowedFilters.insert(allowedFilters.end(), context.allOfFilters.begin(), context.allOfFilters.end());
    allowedFilters.insert(allowedFilters.end(), context.anyOfFilters.begin(), context.anyOfFilters.end());

    std::vector<std::string> blockedMotionTopics{};
    for (const auto& motionTopic : context.recentMotionTopics) {
        const bool matchesAllowSet = std::ranges::any_of(
            allowedFilters,
            [&motionTopic](const std::string& filter) {
                return runtimeMatchesTopicFilter(filter, motionTopic);
            });
        if (!matchesAllowSet) {
            blockedMotionTopics.push_back(motionTopic);
        }
    }

    if (blockedMotionTopics.empty()) {
        appendRuntimeTrace(traceEntries, "allow: passed (all recent motion events allowed)");
        return;
    }

    appendRuntimeTrace(
        traceEntries,
        "allow: failed (motion event not allowed: " + joinText(blockedMotionTopics, ", ") + ")");
}

} // namespace

void appendRuntimeTrace(std::vector<std::string>* traceEntries, const std::string& traceText) {
    if (traceEntries == nullptr) {
        return;
    }
    traceEntries->push_back(traceText);
}

void appendConfiguredEventGateTraceEntries(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    std::vector<std::string>* traceEntries) {
    const bool hasAnyOfField = ruleObject.contains("anyOf");
    const bool hasAllOfField = ruleObject.contains("allOf");
    const bool hasNoneOfField = ruleObject.contains("noneOf");
    const auto allowFiltersOpt = readTopicFilterArrayOnly(ruleObject, "allow");
    const bool hasAllowField = allowFiltersOpt.has_value();
    const auto inactivityMinutes = readNumberField(ruleObject, "durationWithoutMovementInMinutes");
    const bool hasInactivityGate = inactivityMinutes.has_value();

    if (!hasAnyOfField && !hasAllOfField && !hasNoneOfField && !hasAllowField && !hasInactivityGate) {
        return;
    }

    appendInactivityTraceEntry(eventState, evaluationTime, inactivityMinutes, traceEntries);

    if (!hasAnyOfField && !hasAllOfField) {
        return;
    }

    const EventGateTraceContext context = buildEventGateTraceContext(
        ruleObject,
        eventState,
        evaluationTime,
        hasInactivityGate);
    appendAllOfTraceEntry(context, hasAllOfField, traceEntries);
    appendAnyOfTraceEntry(context, hasAnyOfField, traceEntries);
    appendNoneOfTraceEntry(context, hasNoneOfField, traceEntries);
    if (hasAllowField) {
        appendAllowTraceEntry(context, traceEntries);
    }
}

} // namespace yaha
