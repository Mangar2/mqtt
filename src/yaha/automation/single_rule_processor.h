#pragma once

/**
 * @file single_rule_processor.h
 * @brief End-to-end processing for one automation rule object.
 */

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/automation/rules_tree_parser.h"
#include "yaha/message/message.h"

namespace yaha {

/**
 * @brief Result of processing one complete automation rule.
 */
struct SingleRuleProcessingResult {
    bool success{false};                           ///< True when processing completed without errors.
    bool triggered{false};                         ///< True when check condition is active and message is emitted.
    std::optional<Message> message;                ///< Produced outbound message when triggered.
    std::set<std::string> usedVariables;           ///< Variables touched by evaluated expressions.
    std::vector<std::string> errors;               ///< Processing errors.
};

/**
 * @brief Processes one complete rule object including check/value expressions.
 */
class SingleRuleProcessor {
public:
    /**
     * @brief Processes one rule object from the structured rules tree.
     *
     * Required fields:
     * - `topic` as string
     * - `value` as number or expression string
     *
     * Optional fields:
     * - `check` as expression string (defaults to true)
     * - `qos` as number 0..2 (defaults to 1)
     *
     * @param ruleNode Rule object node.
     * @param variables Runtime external variables.
     * @return Processing result with optional emitted message.
     */
    [[nodiscard]] static SingleRuleProcessingResult process(
        const RuleTreeNode& ruleNode,
        const ExpressionEvaluator::VariableMap& variables);
};

} // namespace yaha
