#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/automation/rules_tree_parser.h"

namespace yaha {

[[nodiscard]] bool evaluateWeekdayGate(
    const RuleTreeNode::Object& ruleObject,
    const std::chrono::system_clock::time_point& evaluationTime);
[[nodiscard]] bool evaluateTimeWindowGate(
    const RuleTreeNode::Object& ruleObject,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    std::vector<std::string>* errors);

} // namespace yaha
