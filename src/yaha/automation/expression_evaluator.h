#pragma once

/**
 * @file expression_evaluator.h
 * @brief Recursive evaluator for parsed YAHA automation expressions.
 */

#include <chrono>
#include <map>
#include <string>
#include <variant>

#include "yaha/automation/expression_evaluation_result.h"
#include "yaha/automation/expression_ast.h"

namespace yaha {

/**
 * @brief Evaluates parsed field scripts into runtime values.
 */
class ExpressionEvaluator {
public:
    /**
     * @brief Value type accepted for external variables.
     */
    using Value = std::variant<std::string, double, bool, std::chrono::system_clock::time_point>;

    /**
     * @brief Variable map used as input context during evaluation.
     */
    using VariableMap = std::map<std::string, Value>;

    /**
     * @brief Evaluates one parsed field script recursively.
     *
     * Declaration lines are evaluated in order and can be referenced by map calls
     * in subsequent declarations and in the result expression.
     *
     * @param script Parsed field script AST.
     * @param variables Runtime values for external variable references.
     * @return Evaluation result with value, used variables, and possible errors.
     */
    [[nodiscard]] static ExpressionEvaluationResult evaluate(
        const FieldScriptAst& script,
        const VariableMap& variables);
};

} // namespace yaha
