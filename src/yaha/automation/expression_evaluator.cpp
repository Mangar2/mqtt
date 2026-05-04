#include "yaha/automation/expression_evaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <vector>

namespace yaha {
namespace {

using ExternalVariableMap = ExpressionEvaluator::VariableMap;

struct MapRuntimeEntry {
    bool isDefault{false};
    std::string keyToken;
    ExpressionEvaluator::Value value;
};

struct MapRuntimeValue {
    std::vector<MapRuntimeEntry> entries;
};

using RuntimeValue = std::variant<std::string, double, bool, std::chrono::system_clock::time_point, MapRuntimeValue>;

[[nodiscard]] std::string toLower(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](const unsigned char charValue) {
        return static_cast<char>(std::tolower(charValue));
    });
    return textValue;
}

[[nodiscard]] bool parseDouble(const std::string& tokenText, double* parsedValue) {
    std::size_t parsedLength = 0U;
    try {
        *parsedValue = std::stod(tokenText, &parsedLength);
    } catch (...) {
        return false;
    }
    return parsedLength == tokenText.size();
}

[[nodiscard]] bool tryParseTimeText(const std::string& timeText, std::chrono::seconds* parsedSeconds) {
    int hourValue = -1;
    int minuteValue = -1;
    int secondValue = 0;

    if (std::sscanf(timeText.c_str(), "%d:%d:%d", &hourValue, &minuteValue, &secondValue) == 3) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > 59 || secondValue < 0 || secondValue > 59) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue} + std::chrono::seconds{secondValue};
        return true;
    }

    if (std::sscanf(timeText.c_str(), "%d:%d", &hourValue, &minuteValue) == 2) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > 59) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue};
        return true;
    }

    return false;
}

[[nodiscard]] std::string formatTimeText(const std::chrono::seconds secondsValue) {
    const auto secondCount = secondsValue.count();
    const auto normalizedSeconds = (secondCount % 86400 + 86400) % 86400;
    const int hourValue = static_cast<int>(normalizedSeconds / 3600);
    const int minuteValue = static_cast<int>((normalizedSeconds % 3600) / 60);
    const int secondValue = static_cast<int>(normalizedSeconds % 60);

    std::ostringstream textStream;
    textStream.fill('0');
    textStream.width(2);
    textStream << hourValue << ':';
    textStream.width(2);
    textStream << minuteValue << ':';
    textStream.width(2);
    textStream << secondValue;
    return textStream.str();
}

[[nodiscard]] std::optional<std::chrono::seconds> tryTimeOfDay(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        const auto timePoint = std::get<std::chrono::system_clock::time_point>(runtimeValue);
        const auto dayStart = std::chrono::floor<std::chrono::days>(timePoint);
        return std::chrono::duration_cast<std::chrono::seconds>(timePoint - dayStart);
    }

    if (std::holds_alternative<std::string>(runtimeValue)) {
        std::chrono::seconds parsedSeconds{};
        if (tryParseTimeText(std::get<std::string>(runtimeValue), &parsedSeconds)) {
            return parsedSeconds;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::string valueToString(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::string>(runtimeValue)) {
        return std::get<std::string>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        std::ostringstream textStream;
        textStream << std::get<double>(runtimeValue);
        return textStream.str();
    }
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue) ? "true" : "false";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        const auto secondsValue = tryTimeOfDay(runtimeValue);
        return secondsValue.has_value() ? formatTimeText(*secondsValue) : "time";
    }
    return "map";
}

[[nodiscard]] bool toBool(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        return std::fabs(std::get<double>(runtimeValue)) > 1e-12;
    }
    if (std::holds_alternative<std::string>(runtimeValue)) {
        const std::string loweredValue = toLower(std::get<std::string>(runtimeValue));
        if (loweredValue == "" || loweredValue == "false" || loweredValue == "off" || loweredValue == "0") {
            return false;
        }
        if (loweredValue == "true" || loweredValue == "on" || loweredValue == "1") {
            return true;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool evalEquals(const RuntimeValue& leftValue, const RuntimeValue& rightValue) {
    if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
        return std::fabs(std::get<double>(leftValue) - std::get<double>(rightValue)) < 1e-12;
    }

    const auto leftTime = tryTimeOfDay(leftValue);
    const auto rightTime = tryTimeOfDay(rightValue);
    if (leftTime.has_value() && rightTime.has_value()) {
        return *leftTime == *rightTime;
    }

    return valueToString(leftValue) == valueToString(rightValue);
}

class RuntimeEvaluator {
public:
    RuntimeEvaluator(const FieldScriptAst& scriptAst, const ExternalVariableMap& variableMap)
        : script_(scriptAst), variables_(variableMap) {
        for (const auto& declaration : script_.declarations) {
            declarations_.insert({declaration.name, declaration.entries});
        }
    }

    [[nodiscard]] ExpressionEvaluationResult run() {
        ExpressionEvaluationResult result;

        if (!script_.resultExpression) {
            result.errors.emplace_back("missing result expression");
            result.usedVariables = usedVariables_;
            return result;
        }

        const auto evaluatedValue = eval(script_.resultExpression);
        if (!evaluatedValue.has_value()) {
            result.errors = errors_;
            result.usedVariables = usedVariables_;
            return result;
        }

        if (std::holds_alternative<std::string>(*evaluatedValue)) {
            result.value = std::get<std::string>(*evaluatedValue);
        } else if (std::holds_alternative<double>(*evaluatedValue)) {
            result.value = std::get<double>(*evaluatedValue);
        } else if (std::holds_alternative<bool>(*evaluatedValue)) {
            result.value = std::get<bool>(*evaluatedValue);
        } else if (std::holds_alternative<std::chrono::system_clock::time_point>(*evaluatedValue)) {
            result.value = std::get<std::chrono::system_clock::time_point>(*evaluatedValue);
        } else {
            errors_.emplace_back("result expression must not evaluate to a map");
        }

        result.success = errors_.empty();
        result.errors = errors_;
        result.usedVariables = usedVariables_;
        return result;
    }

private:
    [[nodiscard]] std::optional<RuntimeValue> eval(const ExprPtr& expression) {
        if (!expression) {
            errors_.emplace_back("invalid null expression");
            return std::nullopt;
        }

        if (std::holds_alternative<LiteralNode>(expression->node)) {
            return evalLiteralNode(std::get<LiteralNode>(expression->node));
        }

        if (std::holds_alternative<IdentifierNode>(expression->node)) {
            return evalIdentifierNode(std::get<IdentifierNode>(expression->node));
        }

        if (std::holds_alternative<VariableRefNode>(expression->node)) {
            return evalVariableRefNode(std::get<VariableRefNode>(expression->node));
        }

        if (std::holds_alternative<UnaryOpNode>(expression->node)) {
            return evalUnaryNode(std::get<UnaryOpNode>(expression->node));
        }

        if (std::holds_alternative<BinaryOpNode>(expression->node)) {
            return evalBinaryNode(std::get<BinaryOpNode>(expression->node));
        }

        if (std::holds_alternative<IfCallNode>(expression->node)) {
            return evalIfCallNode(std::get<IfCallNode>(expression->node));
        }

        if (std::holds_alternative<MapLiteralNode>(expression->node)) {
            return evalMapLiteralNode(std::get<MapLiteralNode>(expression->node));
        }

        if (std::holds_alternative<MapCallNode>(expression->node)) {
            return evalMapCallNode(std::get<MapCallNode>(expression->node));
        }

        errors_.emplace_back("unsupported expression node");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeValue> evalLiteralNode(const LiteralNode& literalNode) {
        const auto& literalValue = literalNode.value;
        if (std::holds_alternative<std::string>(literalValue)) {
            const std::string textValue = std::get<std::string>(literalValue);
            if (textValue.find('/') != std::string::npos) {
                const auto variableIterator = variables_.find(textValue);
                if (variableIterator != variables_.end()) {
                    usedVariables_.insert(textValue);
                    if (std::holds_alternative<std::string>(variableIterator->second)) {
                        return RuntimeValue{std::get<std::string>(variableIterator->second)};
                    }
                    if (std::holds_alternative<double>(variableIterator->second)) {
                        return RuntimeValue{std::get<double>(variableIterator->second)};
                    }
                    if (std::holds_alternative<bool>(variableIterator->second)) {
                        return RuntimeValue{std::get<bool>(variableIterator->second)};
                    }
                    return RuntimeValue{std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
                }
            }
            return RuntimeValue{textValue};
        }

        return RuntimeValue{std::get<double>(literalValue)};
    }

    [[nodiscard]] static std::optional<RuntimeValue> evalIdentifierNode(const IdentifierNode& identifierNode) {
        const std::string identifierName = identifierNode.name;
        if (toLower(identifierName) == "true") {
            return RuntimeValue{true};
        }
        if (toLower(identifierName) == "false") {
            return RuntimeValue{false};
        }
        return RuntimeValue{identifierName};
    }

    [[nodiscard]] std::optional<RuntimeValue> evalVariableRefNode(const VariableRefNode& variableNode) {
        const std::string variableName = variableNode.name;
        usedVariables_.insert(variableName);
        const auto variableIterator = variables_.find(variableName);
        if (variableIterator == variables_.end()) {
            errors_.emplace_back("undefined variable: " + variableName);
            return std::nullopt;
        }

        if (std::holds_alternative<std::string>(variableIterator->second)) {
            return RuntimeValue{std::get<std::string>(variableIterator->second)};
        }
        if (std::holds_alternative<double>(variableIterator->second)) {
            return RuntimeValue{std::get<double>(variableIterator->second)};
        }
        if (std::holds_alternative<bool>(variableIterator->second)) {
            return RuntimeValue{std::get<bool>(variableIterator->second)};
        }
        return RuntimeValue{std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
    }

    [[nodiscard]] std::optional<RuntimeValue> evalUnaryNode(const UnaryOpNode& unaryNode) {
        const auto operandValue = eval(unaryNode.operand);
        if (!operandValue.has_value()) {
            return std::nullopt;
        }
        return RuntimeValue{!toBool(*operandValue)};
    }

    [[nodiscard]] std::optional<RuntimeValue> evalBinaryNode(const BinaryOpNode& binaryNode) {
        const auto leftValue = eval(binaryNode.left);
        const auto rightValue = eval(binaryNode.right);
        if (!leftValue.has_value() || !rightValue.has_value()) {
            return std::nullopt;
        }

        switch (binaryNode.op) {
        case BinaryOperator::And:
            return RuntimeValue{toBool(*leftValue) && toBool(*rightValue)};
        case BinaryOperator::Or:
            return RuntimeValue{toBool(*leftValue) || toBool(*rightValue)};
        case BinaryOperator::Eq:
            return RuntimeValue{evalEquals(*leftValue, *rightValue)};
        case BinaryOperator::Neq:
            return RuntimeValue{!evalEquals(*leftValue, *rightValue)};
        case BinaryOperator::Gt:
            return compareRel(*leftValue, *rightValue, "gt");
        case BinaryOperator::Lt:
            return compareRel(*leftValue, *rightValue, "lt");
        case BinaryOperator::Ge:
            return compareRel(*leftValue, *rightValue, "ge");
        case BinaryOperator::Le:
            return compareRel(*leftValue, *rightValue, "le");
        case BinaryOperator::Add:
        case BinaryOperator::Sub:
            return evalAddSub(*leftValue, *rightValue, binaryNode.op == BinaryOperator::Add);
        }

        errors_.emplace_back("unsupported binary operator");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeValue> evalIfCallNode(const IfCallNode& ifCallNode) {
        const auto conditionValue = eval(ifCallNode.condition);
        if (!conditionValue.has_value()) {
            return std::nullopt;
        }
        return toBool(*conditionValue) ? eval(ifCallNode.trueValue) : eval(ifCallNode.falseValue);
    }

    [[nodiscard]] std::optional<RuntimeValue> evalMapLiteralNode(const MapLiteralNode& mapNode) {
        MapRuntimeValue runtimeMap;
        for (const auto& mapEntry : mapNode.entries) {
            const auto entryValue = eval(mapEntry.value);
            if (!entryValue.has_value()) {
                return std::nullopt;
            }
            if (std::holds_alternative<MapRuntimeValue>(*entryValue)) {
                errors_.emplace_back("map entry value must not be a map");
                return std::nullopt;
            }

            if (std::holds_alternative<std::string>(*entryValue)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::string>(*entryValue)});
            } else if (std::holds_alternative<double>(*entryValue)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<double>(*entryValue)});
            } else if (std::holds_alternative<bool>(*entryValue)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<bool>(*entryValue)});
            } else if (std::holds_alternative<std::chrono::system_clock::time_point>(*entryValue)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::chrono::system_clock::time_point>(*entryValue)});
            }
        }

        return RuntimeValue{runtimeMap};
    }

    [[nodiscard]] std::optional<RuntimeValue> evalMapCallNode(const MapCallNode& mapCallNode) {
        const auto declarationIterator = declarations_.find(mapCallNode.name);
        if (declarationIterator == declarations_.end()) {
            errors_.emplace_back("undefined map declaration: " + mapCallNode.name);
            return std::nullopt;
        }

        const auto selectorValue = eval(mapCallNode.selector);
        if (!selectorValue.has_value()) {
            return std::nullopt;
        }

        std::optional<RuntimeValue> defaultValue;
        for (const auto& mapEntry : declarationIterator->second) {
            auto entryValue = eval(mapEntry.value);
            if (!entryValue.has_value()) {
                return std::nullopt;
            }

            if (mapEntry.key.isDefault) {
                defaultValue = entryValue;
                continue;
            }

            if (matchesMapKey(mapEntry.key.token, *selectorValue)) {
                return entryValue;
            }
        }

        if (defaultValue.has_value()) {
            return defaultValue;
        }

        errors_.emplace_back("map call has no matching key and no default: " + mapCallNode.name);
        return std::nullopt;
    }

    [[nodiscard]] static bool matchesMapKey(const std::string& keyToken, const RuntimeValue& selectorValue) {
        double keyNumber = 0.0;
        if (parseDouble(keyToken, &keyNumber) && std::holds_alternative<double>(selectorValue)) {
            return std::fabs(std::get<double>(selectorValue) - keyNumber) < 1e-12;
        }

        std::string normalizedKey = keyToken;
        if (normalizedKey.size() >= 2U && (normalizedKey.front() == '\'' || normalizedKey.front() == '"')
            && normalizedKey.back() == normalizedKey.front()) {
            normalizedKey = normalizedKey.substr(1U, normalizedKey.size() - 2U);
        }

        return normalizedKey == valueToString(selectorValue);
    }

    [[nodiscard]] std::optional<RuntimeValue> compareRel(
        const RuntimeValue& leftValue,
        const RuntimeValue& rightValue,
        const std::string& relationOperator) {
        if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
            const double leftNumber = std::get<double>(leftValue);
            const double rightNumber = std::get<double>(rightValue);
            if (relationOperator == "gt") {
                return RuntimeValue{leftNumber > rightNumber};
            }
            if (relationOperator == "lt") {
                return RuntimeValue{leftNumber < rightNumber};
            }
            if (relationOperator == "ge") {
                return RuntimeValue{leftNumber >= rightNumber};
            }
            return RuntimeValue{leftNumber <= rightNumber};
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        const auto rightTime = tryTimeOfDay(rightValue);
        if (leftTime.has_value() && rightTime.has_value()) {
            if (relationOperator == "gt") {
                return RuntimeValue{*leftTime > *rightTime};
            }
            if (relationOperator == "lt") {
                return RuntimeValue{*leftTime < *rightTime};
            }
            if (relationOperator == "ge") {
                return RuntimeValue{*leftTime >= *rightTime};
            }
            return RuntimeValue{*leftTime <= *rightTime};
        }

        errors_.emplace_back("invalid operands for relational comparison");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RuntimeValue> evalAddSub(
        const RuntimeValue& leftValue,
        const RuntimeValue& rightValue,
        const bool isAddOperation) {
        if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
            const double leftNumber = std::get<double>(leftValue);
            const double rightNumber = std::get<double>(rightValue);
            return RuntimeValue{isAddOperation ? leftNumber + rightNumber : leftNumber - rightNumber};
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        if (leftTime.has_value() && std::holds_alternative<double>(rightValue)) {
            const auto deltaMinutes = std::llround(std::get<double>(rightValue));
            const auto deltaDuration = std::chrono::minutes{deltaMinutes};
            const auto resultTime = isAddOperation ? *leftTime + deltaDuration : *leftTime - deltaDuration;
            if (std::holds_alternative<std::chrono::system_clock::time_point>(leftValue)) {
                const auto dayStart = std::chrono::floor<std::chrono::days>(std::get<std::chrono::system_clock::time_point>(leftValue));
                return RuntimeValue{dayStart + resultTime};
            }
            return RuntimeValue{formatTimeText(resultTime)};
        }

        errors_.emplace_back("invalid operands for arithmetic operation");
        return std::nullopt;
    }

    const FieldScriptAst& script_;
    const ExternalVariableMap& variables_;
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
