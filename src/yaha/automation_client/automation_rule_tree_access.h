#pragma once

/**
 * @file automation_rule_tree_access.h
 * @brief Helpers for accessing and normalizing automation rule trees.
 */

#include <optional>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha::automation_rule_tree_access {

[[nodiscard]] RuleTreeNode::Object* ensureRulesObject(RuleTreeNode* rootNode);
[[nodiscard]] RuleTreeNode::Object* ensureRuleContainerByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& parentPathSegments);
[[nodiscard]] RuleTreeNode::Object* findRuleContainerByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& parentPathSegments);
[[nodiscard]] const RuleTreeNode* findRuleByPath(
    const RuleTreeNode& rootNode,
    const std::vector<std::string>& fullRulePathSegments);
void upsertRuleByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& fullRulePathSegments,
    RuleTreeNode ruleNode);
[[nodiscard]] bool eraseRuleByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& fullRulePathSegments);
[[nodiscard]] std::optional<std::string> readStringField(
    const RuleTreeNode::Object& objectNode,
    const std::string& fieldName);

} // namespace yaha::automation_rule_tree_access
