#pragma once

/**
 * @file expression_evaluator_helpers.h
 * @brief Runtime value types and free helper functions used by ExpressionEvaluator.
 */

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace yaha {

/**
 * @brief Tolerance used when comparing floating point runtime values for equality.
 */
constexpr double k_numeric_epsilon{1e-12};

/**
 * @brief One key/value entry of an evaluated map literal or map declaration.
 */
struct MapRuntimeEntry {
    bool isDefault{false};  ///< True if this entry is the map's default fallback entry.
    std::string keyToken;  ///< Raw key token as written in the source map.
    std::variant<std::string, double, bool, std::chrono::system_clock::time_point> value;  ///< Evaluated entry value.
};

/**
 * @brief Runtime representation of an evaluated map literal.
 */
struct MapRuntimeValue {
    std::vector<MapRuntimeEntry> entries;  ///< Ordered map entries as evaluated at runtime.
};

/**
 * @brief Runtime value produced while evaluating an expression tree.
 */
using RuntimeValue = std::variant<std::string, double, bool, std::chrono::system_clock::time_point, MapRuntimeValue>;

/**
 * @brief Evaluated expression node: its runtime value plus a human-readable reason trace.
 */
struct EvaluatedNode {
    RuntimeValue value;  ///< Evaluated runtime value.
    std::string reason;  ///< Human-readable trace of how the value was derived.
};

/**
 * @brief Parses a full token as a double, rejecting partial matches.
 * @param tokenText Text to parse.
 * @param parsedValue Receives the parsed value on success.
 * @return True if tokenText was fully consumed as a valid double.
 */
[[nodiscard]] bool parseDouble(const std::string& tokenText, double* parsedValue);

/**
 * @brief Attempts to interpret a runtime value as a JavaScript-like numeric value.
 * @param runtimeValue Value to interpret.
 * @return Parsed number, or std::nullopt if runtimeValue is not numeric-like.
 */
[[nodiscard]] std::optional<double> tryNumericLikeJs(const RuntimeValue& runtimeValue);

/**
 * @brief Attempts to derive a local time-of-day duration from a runtime value.
 * @param runtimeValue Value to interpret, either a time point or a time/ISO instant string.
 * @return Time of day since local midnight, or std::nullopt if not derivable.
 */
[[nodiscard]] std::optional<std::chrono::seconds> tryTimeOfDay(const RuntimeValue& runtimeValue);

/**
 * @brief Computes local midnight of the day containing the given time point.
 * @param timePoint Time point to derive the local day start from.
 * @return Local midnight time point, or std::nullopt if local time conversion fails.
 */
[[nodiscard]] std::optional<std::chrono::system_clock::time_point> localDayStart(
    const std::chrono::system_clock::time_point& timePoint);

/**
 * @brief Formats a duration since midnight as an "HH:MM:SS" text.
 * @param secondsValue Duration since midnight, normalized modulo one day.
 * @return Formatted "HH:MM:SS" text.
 */
[[nodiscard]] std::string formatTimeText(std::chrono::seconds secondsValue);

/**
 * @brief Converts a runtime value to its display text form.
 * @param runtimeValue Value to convert.
 * @return Display text for the value.
 */
[[nodiscard]] std::string valueToString(const RuntimeValue& runtimeValue);

/**
 * @brief Returns the display name of a runtime value's type.
 * @param runtimeValue Value to inspect.
 * @return One of "string", "number", "bool", "time", or "map".
 */
[[nodiscard]] std::string valueTypeToString(const RuntimeValue& runtimeValue);

/**
 * @brief Extracts the variable name from a reason text marked as undefined.
 * @param reasonText Reason text produced during evaluation.
 * @return Variable name, or an empty string if reasonText does not mark an undefined variable.
 */
[[nodiscard]] std::string extractUndefinedVariableName(const std::string& reasonText);

/**
 * @brief Maps a relational operator name to its display symbol.
 * @param relationOperator Operator name: "gt", "lt", "ge", or "le".
 * @return Display symbol such as ">" or "<=", or "?" if unknown.
 */
[[nodiscard]] std::string relationOperatorSymbol(const std::string& relationOperator);

/**
 * @brief Converts a runtime value to its truthy/falsy boolean interpretation.
 * @param runtimeValue Value to convert.
 * @return Boolean interpretation of runtimeValue.
 */
[[nodiscard]] bool toBool(const RuntimeValue& runtimeValue);

/**
 * @brief Evaluates equality between two runtime values, comparing numerically or by time when possible.
 * @param leftValue Left-hand operand.
 * @param rightValue Right-hand operand.
 * @return True if leftValue and rightValue are considered equal.
 */
[[nodiscard]] bool evalEquals(const RuntimeValue& leftValue, const RuntimeValue& rightValue);

/**
 * @brief Appends the reason for a failed relational comparison to an error stream.
 * @param errorText Stream to append the cause description to.
 * @param leftUndefinedVariable Name of the left operand's undefined variable, or empty if none.
 * @param rightUndefinedVariable Name of the right operand's undefined variable, or empty if none.
 */
void appendUndefinedCause(
    std::ostringstream* errorText,
    const std::string& leftUndefinedVariable,
    const std::string& rightUndefinedVariable);

/**
 * @brief Evaluates a relational operator between two comparable values.
 * @tparam ComparableType Type supporting the relational operators.
 * @param leftValue Left-hand operand.
 * @param rightValue Right-hand operand.
 * @param relationOperator Operator name: "gt", "lt", "ge", or "le".
 * @return Result of applying relationOperator to leftValue and rightValue.
 */
template <typename ComparableType>
[[nodiscard]] bool evaluateRelation(
    const ComparableType& leftValue,
    const ComparableType& rightValue,
    const std::string& relationOperator) {
    if (relationOperator == "gt") {
        return leftValue > rightValue;
    }
    if (relationOperator == "lt") {
        return leftValue < rightValue;
    }
    if (relationOperator == "ge") {
        return leftValue >= rightValue;
    }
    return leftValue <= rightValue;
}

} // namespace yaha
