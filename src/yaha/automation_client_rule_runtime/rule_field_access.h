#pragma once

#include <optional>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha {

[[nodiscard]] std::optional<double> readNumberField(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName);
[[nodiscard]] std::vector<std::string> readTopicFilterList(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName);
[[nodiscard]] std::optional<std::vector<std::string>> readTopicFilterArrayOnly(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName);
[[nodiscard]] bool fieldIsArray(const RuleTreeNode::Object& ruleObject, const std::string& fieldName);
[[nodiscard]] bool readActiveFlag(const RuleTreeNode::Object& ruleObject);

} // namespace yaha
