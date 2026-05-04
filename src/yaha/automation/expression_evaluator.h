#pragma once

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "yaha/automation/expression_ast.h"

namespace yaha {

struct MapRuntimeEntry {
    bool isDefault{false};
    std::string keyToken;
    std::variant<std::string, double, bool, std::chrono::system_clock::time_point> value;
};

struct MapRuntimeValue {
    std::vector<MapRuntimeEntry> entries;
};

struct ExpressionEvaluationResult {
    using Value = std::variant<std::string, double, bool, std::chrono::system_clock::time_point, MapRuntimeValue>;

    bool success{false};
    Value value{std::string{}};
    std::set<std::string> usedVariables;
    std::vector<std::string> errors;
};

class ExpressionEvaluator {
public:
    using Value = std::variant<std::string, double, bool, std::chrono::system_clock::time_point>;
    using VariableMap = std::map<std::string, Value>;

    /**
     * @brief Evaluates one parsed field script recursively.
     *
     * Declaration lines are evaluated in order and can be referenced by map calls
     * in subsequent declarations and in the result expression.
     */
    [[nodiscard]] static ExpressionEvaluationResult evaluate(
        const FieldScriptAst& script,
        const VariableMap& variables);
};

} // namespace yaha
