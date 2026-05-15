#pragma once

/**
 * @file automation_rule_lookup.h
 * @brief Helper functions for rule-link path resolution in automation client.
 */

#include <optional>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha::automation_rule_lookup {

[[nodiscard]] std::vector<std::string> splitPathSegments(const std::string& pathText);
[[nodiscard]] std::string joinPathSegments(const std::vector<std::string>& segments);
[[nodiscard]] std::vector<std::vector<std::string>> buildRuleLookupCandidates(
    const std::vector<std::string>& ruleSegments);
[[nodiscard]] std::optional<RuleTreeNode> findNodeByPathSegments(
    const RuleTreeNode& rootNode,
    const std::vector<std::string>& segments);

} // namespace yaha::automation_rule_lookup
