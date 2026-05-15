#pragma once

/**
 * @file automation_control_topics.h
 * @brief Helpers for control-topic matching and management payload handling.
 */

#include <string>
#include <optional>

namespace yaha::automation_control_topics {

[[nodiscard]] bool startsWithText(const std::string& textValue, const std::string& prefix);
[[nodiscard]] bool endsWithSetSuffix(const std::string& textValue);
[[nodiscard]] bool endsWithDebugSuffix(const std::string& textValue);
[[nodiscard]] bool isDeletePayloadText(const std::string& payload);
[[nodiscard]] bool isMonitoringTopic(const std::string& topicName, const std::string& monitorTopicPrefix);
[[nodiscard]] bool isManagementTopic(const std::string& topicName, const std::string& managementTopicPrefix);
[[nodiscard]] bool isDebugTopic(const std::string& topicName, const std::string& debugTopicPrefix);
[[nodiscard]] std::optional<std::string> extractRuleNameFromManagementTopic(
	const std::string& topicName,
	const std::string& managementTopicPrefix);
[[nodiscard]] std::optional<std::string> extractRuleLinkFromDebugTopic(
	const std::string& topicName,
	const std::string& debugTopicPrefix);
[[nodiscard]] std::string buildTraceTopicFromRuleLink(
	const std::string& debugTopicPrefix,
	const std::string& ruleLink);

} // namespace yaha::automation_control_topics
