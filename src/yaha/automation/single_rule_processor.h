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
    std::vector<Message> messages;                 ///< Produced outbound messages when triggered.
    std::optional<Message> message;                ///< Legacy first outbound message when triggered.
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
    * - `topic` as string, array of strings, or object map topic -> value
    * - `value` as number or expression string when `topic` is a string or array
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
        const ExpressionEvaluator::VariableMap& variables,
        const std::string& ruleIdentifier = "");

    /**
     * @brief Processes one rule object and emits a detailed decision trace.
     *
     * The trace contains one line per relevant decision step used to determine
     * whether a rule emits an outbound message.
     *
     * @param ruleNode Rule object node.
     * @param variables Runtime external variables.
     * @param traceEntries Output trace lines in evaluation order.
     * @return Processing result with optional emitted message.
     */
    [[nodiscard]] static SingleRuleProcessingResult processWithTrace(
        const RuleTreeNode& ruleNode,
        const ExpressionEvaluator::VariableMap& variables,
        std::vector<std::string>* traceEntries,
        const std::string& ruleIdentifier = "");
};

} // namespace yaha
