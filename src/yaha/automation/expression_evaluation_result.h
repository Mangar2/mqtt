#pragma once

/**
 * @file expression_evaluation_result.h
 * @brief Result object for evaluated automation expressions.
 */

#include <chrono>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace yaha {

/**
 * @brief Stores the result of a single expression evaluation run.
 */
struct ExpressionEvaluationResult {
    /**
     * @brief Runtime value type that can be emitted by expression evaluation.
     */
    using Value = std::variant<std::string, double, bool, std::chrono::system_clock::time_point>;

    bool success{false};                         ///< True when evaluation finished without errors.
    Value value{std::string{}};                 ///< Result value of the script expression.
    std::set<std::string> usedVariables;        ///< Variable names read during evaluation.
    std::vector<std::string> errors;            ///< Error messages collected during evaluation.
};

} // namespace yaha
