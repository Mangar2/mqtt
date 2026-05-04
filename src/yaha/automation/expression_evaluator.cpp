#include "yaha/automation/expression_evaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>

namespace yaha {
namespace {

using EvalValue = ExpressionEvaluationResult::Value;
using VariableMap = ExpressionEvaluator::VariableMap;

[[nodiscard]] std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

[[nodiscard]] bool parseDouble(const std::string& token, double* value) {
    std::size_t parsed = 0U;
    try {
        *value = std::stod(token, &parsed);
    } catch (...) {
        return false;
    }
    return parsed == token.size();
}

[[nodiscard]] bool tryParseTimeText(const std::string& token, std::chrono::seconds* out) {
    int hour = -1;
    int minute = -1;
    int second = 0;

    if (std::sscanf(token.c_str(), "%d:%d:%d", &hour, &minute, &second) == 3) {
        if (hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59) {
            return false;
        }
        *out = std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
        return true;
    }

    if (std::sscanf(token.c_str(), "%d:%d", &hour, &minute) == 2) {
        if (hour < 0 || minute < 0 || minute > 59) {
            return false;
        }
        *out = std::chrono::hours{hour} + std::chrono::minutes{minute};
        return true;
    }

    return false;
}

[[nodiscard]] std::string formatTimeText(const std::chrono::seconds value) {
    const auto total = value.count();
    const auto norm = (total % 86400 + 86400) % 86400;
    const int hour = static_cast<int>(norm / 3600);
    const int minute = static_cast<int>((norm % 3600) / 60);
    const int second = static_cast<int>(norm % 60);

    std::ostringstream text;
    text.fill('0');
    text.width(2);
    text << hour << ':';
    text.width(2);
    text << minute << ':';
    text.width(2);
    text << second;
    return text.str();
}

[[nodiscard]] std::optional<std::chrono::seconds> tryTimeOfDay(const EvalValue& value) {
    if (std::holds_alternative<std::chrono::system_clock::time_point>(value)) {
        const auto point = std::get<std::chrono::system_clock::time_point>(value);
        const auto day = std::chrono::floor<std::chrono::days>(point);
        return std::chrono::duration_cast<std::chrono::seconds>(point - day);
    }

    if (std::holds_alternative<std::string>(value)) {
        std::chrono::seconds parsed{};
        if (tryParseTimeText(std::get<std::string>(value), &parsed)) {
            return parsed;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::string valueToString(const EvalValue& value) {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    if (std::holds_alternative<double>(value)) {
        std::ostringstream stream;
        stream << std::get<double>(value);
        return stream.str();
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(value)) {
        const auto seconds = tryTimeOfDay(value);
        return seconds.has_value() ? formatTimeText(*seconds) : "time";
    }
    return "map";
}

[[nodiscard]] bool toBool(const EvalValue& value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    if (std::holds_alternative<double>(value)) {
        return std::fabs(std::get<double>(value)) > 1e-12;
    }
    if (std::holds_alternative<std::string>(value)) {
        const std::string lowered = toLower(std::get<std::string>(value));
        if (lowered == "" || lowered == "false" || lowered == "off" || lowered == "0") {
            return false;
        }
        if (lowered == "true" || lowered == "on" || lowered == "1") {
            return true;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool evalEquals(const EvalValue& left, const EvalValue& right) {
    if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
        return std::fabs(std::get<double>(left) - std::get<double>(right)) < 1e-12;
    }

    const auto leftTime = tryTimeOfDay(left);
    const auto rightTime = tryTimeOfDay(right);
    if (leftTime.has_value() && rightTime.has_value()) {
        return *leftTime == *rightTime;
    }

    return valueToString(left) == valueToString(right);
}

class RuntimeEvaluator {
public:
    RuntimeEvaluator(const FieldScriptAst& script, const VariableMap& variables)
        : script_(script), variables_(variables) {
        for (const auto& declaration : script.declarations) {
            declarations_.insert({declaration.name, declaration.entries});
        }
    }

    [[nodiscard]] ExpressionEvaluationResult run() {
        ExpressionEvaluationResult result;

        if (!script_.resultExpression) {
            result.errors.push_back("missing result expression");
            result.usedVariables = usedVariables_;
            return result;
        }

        const auto value = eval(script_.resultExpression);
        if (!value.has_value()) {
            result.errors = errors_;
            result.usedVariables = usedVariables_;
            return result;
        }

        result.success = errors_.empty();
        result.value = *value;
        result.errors = errors_;
        result.usedVariables = usedVariables_;
        return result;
    }

private:
    [[nodiscard]] std::optional<EvalValue> eval(const ExprPtr& expression) {
        if (!expression) {
            errors_.push_back("invalid null expression");
            return std::nullopt;
        }

        if (std::holds_alternative<LiteralNode>(expression->node)) {
            const auto& literal = std::get<LiteralNode>(expression->node).value;
            if (std::holds_alternative<std::string>(literal)) {
                const std::string text = std::get<std::string>(literal);
                if (text.find('/') != std::string::npos) {
                    const auto variable = variables_.find(text);
                    if (variable != variables_.end()) {
                        usedVariables_.insert(text);
                        if (std::holds_alternative<std::string>(variable->second)) {
                            return EvalValue{std::get<std::string>(variable->second)};
                        }
                        if (std::holds_alternative<double>(variable->second)) {
                            return EvalValue{std::get<double>(variable->second)};
                        }
                        if (std::holds_alternative<bool>(variable->second)) {
                            return EvalValue{std::get<bool>(variable->second)};
                        }
                        return EvalValue{std::get<std::chrono::system_clock::time_point>(variable->second)};
                    }
                }
                return EvalValue{text};
            }
            return EvalValue{std::get<double>(literal)};
        }

        if (std::holds_alternative<IdentifierNode>(expression->node)) {
            const std::string name = std::get<IdentifierNode>(expression->node).name;
            if (toLower(name) == "true") {
                return EvalValue{true};
            }
            if (toLower(name) == "false") {
                return EvalValue{false};
            }
            return EvalValue{name};
        }

        if (std::holds_alternative<VariableRefNode>(expression->node)) {
            const std::string name = std::get<VariableRefNode>(expression->node).name;
            usedVariables_.insert(name);
            const auto it = variables_.find(name);
            if (it == variables_.end()) {
                errors_.push_back("undefined variable: " + name);
                return std::nullopt;
            }

            if (std::holds_alternative<std::string>(it->second)) {
                return EvalValue{std::get<std::string>(it->second)};
            }
            if (std::holds_alternative<double>(it->second)) {
                return EvalValue{std::get<double>(it->second)};
            }
            if (std::holds_alternative<bool>(it->second)) {
                return EvalValue{std::get<bool>(it->second)};
            }
            return EvalValue{std::get<std::chrono::system_clock::time_point>(it->second)};
        }

        if (std::holds_alternative<UnaryOpNode>(expression->node)) {
            const auto& unary = std::get<UnaryOpNode>(expression->node);
            const auto value = eval(unary.operand);
            if (!value.has_value()) {
                return std::nullopt;
            }
            return EvalValue{!toBool(*value)};
        }

        if (std::holds_alternative<BinaryOpNode>(expression->node)) {
            const auto& binary = std::get<BinaryOpNode>(expression->node);
            const auto left = eval(binary.left);
            const auto right = eval(binary.right);
            if (!left.has_value() || !right.has_value()) {
                return std::nullopt;
            }

            switch (binary.op) {
            case BinaryOperator::And:
                return EvalValue{toBool(*left) && toBool(*right)};
            case BinaryOperator::Or:
                return EvalValue{toBool(*left) || toBool(*right)};
            case BinaryOperator::Eq:
                return EvalValue{evalEquals(*left, *right)};
            case BinaryOperator::Neq:
                return EvalValue{!evalEquals(*left, *right)};
            case BinaryOperator::Gt:
                return compareRel(*left, *right, "gt");
            case BinaryOperator::Lt:
                return compareRel(*left, *right, "lt");
            case BinaryOperator::Ge:
                return compareRel(*left, *right, "ge");
            case BinaryOperator::Le:
                return compareRel(*left, *right, "le");
            case BinaryOperator::Add:
            case BinaryOperator::Sub:
                return evalAddSub(*left, *right, binary.op == BinaryOperator::Add);
            }
        }

        if (std::holds_alternative<IfCallNode>(expression->node)) {
            const auto& ifCall = std::get<IfCallNode>(expression->node);
            const auto condition = eval(ifCall.condition);
            if (!condition.has_value()) {
                return std::nullopt;
            }
            return toBool(*condition) ? eval(ifCall.trueValue) : eval(ifCall.falseValue);
        }

        if (std::holds_alternative<MapLiteralNode>(expression->node)) {
            const auto& literal = std::get<MapLiteralNode>(expression->node);
            MapRuntimeValue mapValue;
            for (const auto& entry : literal.entries) {
                const auto value = eval(entry.value);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (std::holds_alternative<MapRuntimeValue>(*value)) {
                    errors_.push_back("map entry value must not be a map");
                    return std::nullopt;
                }

                if (std::holds_alternative<std::string>(*value)) {
                    mapValue.entries.push_back(MapRuntimeEntry{entry.key.isDefault, entry.key.token, std::get<std::string>(*value)});
                } else if (std::holds_alternative<double>(*value)) {
                    mapValue.entries.push_back(MapRuntimeEntry{entry.key.isDefault, entry.key.token, std::get<double>(*value)});
                } else if (std::holds_alternative<bool>(*value)) {
                    mapValue.entries.push_back(MapRuntimeEntry{entry.key.isDefault, entry.key.token, std::get<bool>(*value)});
                } else if (std::holds_alternative<std::chrono::system_clock::time_point>(*value)) {
                    mapValue.entries.push_back(MapRuntimeEntry{entry.key.isDefault, entry.key.token, std::get<std::chrono::system_clock::time_point>(*value)});
                }
            }
            return EvalValue{mapValue};
        }

        if (std::holds_alternative<MapCallNode>(expression->node)) {
            const auto& mapCall = std::get<MapCallNode>(expression->node);
            const auto declarationIt = declarations_.find(mapCall.name);
            if (declarationIt == declarations_.end()) {
                errors_.push_back("undefined map declaration: " + mapCall.name);
                return std::nullopt;
            }

            const auto selector = eval(mapCall.selector);
            if (!selector.has_value()) {
                return std::nullopt;
            }

            std::optional<EvalValue> defaultValue;
            for (const auto& entry : declarationIt->second) {
                const auto value = eval(entry.value);
                if (!value.has_value()) {
                    return std::nullopt;
                }

                if (entry.key.isDefault) {
                    defaultValue = *value;
                    continue;
                }

                if (matchesMapKey(entry.key.token, *selector)) {
                    return *value;
                }
            }

            if (defaultValue.has_value()) {
                return *defaultValue;
            }

            errors_.push_back("map call has no matching key and no default: " + mapCall.name);
            return std::nullopt;
        }

        errors_.push_back("unsupported expression node");
        return std::nullopt;
    }

    [[nodiscard]] bool matchesMapKey(const std::string& keyToken, const EvalValue& selector) {
        double keyNumber = 0.0;
        if (parseDouble(keyToken, &keyNumber) && std::holds_alternative<double>(selector)) {
            return std::fabs(std::get<double>(selector) - keyNumber) < 1e-12;
        }

        std::string normalizedKey = keyToken;
        if (normalizedKey.size() >= 2U && (normalizedKey.front() == '\'' || normalizedKey.front() == '"')
            && normalizedKey.back() == normalizedKey.front()) {
            normalizedKey = normalizedKey.substr(1U, normalizedKey.size() - 2U);
        }

        return normalizedKey == valueToString(selector);
    }

    [[nodiscard]] std::optional<EvalValue> compareRel(const EvalValue& left, const EvalValue& right, const std::string& op) {
        if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
            const double lhs = std::get<double>(left);
            const double rhs = std::get<double>(right);
            if (op == "gt") {
                return EvalValue{lhs > rhs};
            }
            if (op == "lt") {
                return EvalValue{lhs < rhs};
            }
            if (op == "ge") {
                return EvalValue{lhs >= rhs};
            }
            return EvalValue{lhs <= rhs};
        }

        const auto leftTime = tryTimeOfDay(left);
        const auto rightTime = tryTimeOfDay(right);
        if (leftTime.has_value() && rightTime.has_value()) {
            if (op == "gt") {
                return EvalValue{*leftTime > *rightTime};
            }
            if (op == "lt") {
                return EvalValue{*leftTime < *rightTime};
            }
            if (op == "ge") {
                return EvalValue{*leftTime >= *rightTime};
            }
            return EvalValue{*leftTime <= *rightTime};
        }

        errors_.push_back("invalid operands for relational comparison");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvalValue> evalAddSub(const EvalValue& left, const EvalValue& right, const bool isAdd) {
        if (std::holds_alternative<double>(left) && std::holds_alternative<double>(right)) {
            const double lhs = std::get<double>(left);
            const double rhs = std::get<double>(right);
            return EvalValue{isAdd ? lhs + rhs : lhs - rhs};
        }

        const auto leftTime = tryTimeOfDay(left);
        if (leftTime.has_value() && std::holds_alternative<double>(right)) {
            const auto deltaMinutes = static_cast<long long>(std::llround(std::get<double>(right)));
            const auto delta = std::chrono::minutes{deltaMinutes};
            const auto result = isAdd ? *leftTime + delta : *leftTime - delta;
            if (std::holds_alternative<std::chrono::system_clock::time_point>(left)) {
                const auto day = std::chrono::floor<std::chrono::days>(std::get<std::chrono::system_clock::time_point>(left));
                return EvalValue{day + result};
            }
            return EvalValue{formatTimeText(result)};
        }

        errors_.push_back("invalid operands for arithmetic operation");
        return std::nullopt;
    }

    const FieldScriptAst& script_;
    const VariableMap& variables_;
    std::map<std::string, std::vector<MapEntryAst>> declarations_;
    std::set<std::string> usedVariables_;
    std::vector<std::string> errors_;
};

} // namespace

ExpressionEvaluationResult ExpressionEvaluator::evaluate(
    const FieldScriptAst& script,
    const VariableMap& variables) {
    RuntimeEvaluator evaluator{script, variables};
    return evaluator.run();
}

} // namespace yaha
