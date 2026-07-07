#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha {

[[nodiscard]] std::string joinPath(const std::string& basePath, const std::string& segment);
[[nodiscard]] bool isRuleNode(const RuleTreeNode& node);
[[nodiscard]] bool topicShapeValid(const RuleTreeNode& topicNode);
[[nodiscard]] bool runtimeMatchesTopicFilter(std::string_view topicFilter, std::string_view topicName);
[[nodiscard]] bool isMotionTopic(const std::string& topicName, const std::vector<std::string>& motionTopicFilters);

} // namespace yaha
