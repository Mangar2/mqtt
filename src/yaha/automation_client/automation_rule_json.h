#pragma once

/**
 * @file automation_rule_json.h
 * @brief JSON parsing and serialization helpers for rule tree payloads.
 */

#include <optional>
#include <string>

#include "yaha/automation/rules_tree_parser.h"

namespace yaha::automation_rule_json {

[[nodiscard]] std::optional<RuleTreeNode> parseJsonNode(const std::string& payload);
[[nodiscard]] std::string toJsonText(const RuleTreeNode& node);
[[nodiscard]] std::optional<std::string> extractStringFieldFromObjectPayload(
    const std::string& payload,
    const std::string& fieldName);

} // namespace yaha::automation_rule_json
