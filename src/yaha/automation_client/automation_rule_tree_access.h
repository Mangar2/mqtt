#pragma once

/**
 * @file automation_rule_tree_access.h
 * @brief Helpers for accessing and normalizing automation rule trees.
 */

#include <optional>
#include <string>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha::automation_rule_tree_access {

[[nodiscard]] RuleTreeNode::Object* ensureRulesObject(RuleTreeNode* rootNode);
[[nodiscard]] std::optional<std::string> readStringField(
    const RuleTreeNode::Object& objectNode,
    const std::string& fieldName);

} // namespace yaha::automation_rule_tree_access
