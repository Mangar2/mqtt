#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation_client_rule_runtime/rule_runtime_engine.h"

namespace yaha {

[[nodiscard]] bool anyEventMatches(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters);
[[nodiscard]] bool allFiltersMatchAnyEvent(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters);
[[nodiscard]] std::set<std::string> collectRecentEventTopics(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    bool inactivityGateConfigured);
[[nodiscard]] std::set<std::string> collectRecentMotionTopics(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    bool inactivityGateConfigured);
[[nodiscard]] std::vector<MotionEventRecord> collectRecentMotionEvents(
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    bool inactivityGateConfigured);
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> findLatestMotionTimestamp(
    const RuleRuntimeEventState& eventState);
[[nodiscard]] std::vector<std::string> collectMatchingTopics(
    const std::set<std::string>& recentEventTopics,
    const std::vector<std::string>& topicFilters);
[[nodiscard]] std::string joinText(const std::vector<std::string>& parts, std::string_view separator);
[[nodiscard]] std::vector<std::string> formatMatchedTopicsWithOptionalTimestamp(
    const std::vector<std::string>& matchedTopics,
    const std::vector<MotionEventRecord>& recentMotionEvents);
[[nodiscard]] std::optional<std::string> buildEventTriggerReason(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime);
[[nodiscard]] bool evaluateEventGates(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime);

} // namespace yaha
