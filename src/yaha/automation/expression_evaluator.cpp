#include "yaha/automation/expression_evaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <optional>
#include <sstream>
#include <vector>

namespace yaha {
namespace {

constexpr int k_max_minutes_or_seconds{59};
constexpr double k_numeric_epsilon{1e-12};

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

struct EvaluatedNode {
    RuntimeValue value;
    std::string reason;
};

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
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds || secondValue < 0
            || secondValue > k_max_minutes_or_seconds) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue} + std::chrono::seconds{secondValue};
        return true;
    }

    if (std::sscanf(timeText.c_str(), "%d:%d", &hourValue, &minuteValue) == 2) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds) {
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

[[nodiscard]] std::optional<std::tm> toLocalCalendarTime(
    const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    if (localtime_s(&localCalendarTime, &epochSeconds) != 0) {
        return std::nullopt;
    }
#else
    if (localtime_r(&epochSeconds, &localCalendarTime) == nullptr) {
        return std::nullopt;
    }
#endif
    return localCalendarTime;
}

[[nodiscard]] std::optional<std::chrono::seconds> localTimeOfDay(
    const std::chrono::system_clock::time_point& timePoint) {
    const auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    return std::chrono::hours{localCalendarTime->tm_hour}
        + std::chrono::minutes{localCalendarTime->tm_min}
        + std::chrono::seconds{localCalendarTime->tm_sec};
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> localDayStart(
    const std::chrono::system_clock::time_point& timePoint) {
    auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    localCalendarTime->tm_hour = 0;
    localCalendarTime->tm_min = 0;
    localCalendarTime->tm_sec = 0;
    localCalendarTime->tm_isdst = -1;
    const std::time_t localMidnightSeconds = std::mktime(&*localCalendarTime);
    if (localMidnightSeconds == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(localMidnightSeconds);
}

[[nodiscard]] std::optional<std::chrono::seconds> tryTimeOfDay(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        return localTimeOfDay(std::get<std::chrono::system_clock::time_point>(runtimeValue));
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
        return std::fabs(std::get<double>(runtimeValue)) > k_numeric_epsilon;
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
        return std::fabs(std::get<double>(leftValue) - std::get<double>(rightValue)) < k_numeric_epsilon;
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

        const auto evaluatedNode = eval(script_.resultExpression);
        if (!evaluatedNode.has_value()) {
            result.errors = errors_;
            result.usedVariables = usedVariables_;
            return result;
        }

        if (std::holds_alternative<std::string>(evaluatedNode->value)) {
            result.value = std::get<std::string>(evaluatedNode->value);
        } else if (std::holds_alternative<double>(evaluatedNode->value)) {
            result.value = std::get<double>(evaluatedNode->value);
        } else if (std::holds_alternative<bool>(evaluatedNode->value)) {
            result.value = std::get<bool>(evaluatedNode->value);
        } else if (std::holds_alternative<std::chrono::system_clock::time_point>(evaluatedNode->value)) {
            result.value = std::get<std::chrono::system_clock::time_point>(evaluatedNode->value);
        } else {
            errors_.emplace_back("result expression must not evaluate to a map");
        }

        result.reason = evaluatedNode->reason;
        if (!missingVariables_.empty()) {
            result.value = false;
            result.reason = buildMissingVariablesReason();
        }
        result.success = errors_.empty();
        result.errors = errors_;
        result.usedVariables = usedVariables_;
        return result;
    }

private:
    [[nodiscard]] std::optional<EvaluatedNode> eval(const ExprPtr& expression) {
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

    [[nodiscard]] std::optional<EvaluatedNode> evalLiteralNode(const LiteralNode& literalNode) {
        const auto& literalValue = literalNode.value;
        if (std::holds_alternative<std::string>(literalValue)) {
            const std::string textValue = std::get<std::string>(literalValue);
            if (textValue.find('/') != std::string::npos) {
                const auto variableIterator = variables_.find(textValue);
                if (variableIterator != variables_.end()) {
                    usedVariables_.insert(textValue);
                    if (std::holds_alternative<std::string>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<std::string>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    if (std::holds_alternative<double>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<double>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    if (std::holds_alternative<bool>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<bool>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    const RuntimeValue variableValue{
                        std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
                    return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                }
            }
            return EvaluatedNode{.value = RuntimeValue{textValue}, .reason = textValue};
        }

        const RuntimeValue numberValue{std::get<double>(literalValue)};
        return EvaluatedNode{.value = numberValue, .reason = valueToString(numberValue)};
    }

    [[nodiscard]] static std::optional<EvaluatedNode> evalIdentifierNode(const IdentifierNode& identifierNode) {
        const std::string identifierName = identifierNode.name;
        if (toLower(identifierName) == "true") {
            return EvaluatedNode{.value = RuntimeValue{true}, .reason = "constant true"};
        }
        if (toLower(identifierName) == "false") {
            return EvaluatedNode{.value = RuntimeValue{false}, .reason = "constant false"};
        }
        return EvaluatedNode{.value = RuntimeValue{identifierName}, .reason = identifierName};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalVariableRefNode(const VariableRefNode& variableNode) {
        const std::string variableName = variableNode.name;
        usedVariables_.insert(variableName);
        const auto variableIterator = variables_.find(variableName);
        if (variableIterator == variables_.end()) {
            missingVariables_.insert(variableName);
            return EvaluatedNode{.value = RuntimeValue{false}, .reason = variableName + " (undefined)"};
        }

        if (std::holds_alternative<std::string>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<std::string>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        if (std::holds_alternative<double>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<double>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        if (std::holds_alternative<bool>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<bool>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        const RuntimeValue variableValue{std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
        return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
    }

    [[nodiscard]] std::string buildMissingVariablesReason() const {
        std::string reasonText{"false, undefined variables: "};
        std::string separator;
        for (const auto& variableName : missingVariables_) {
            reasonText.append(separator);
            reasonText.append(variableName);
            separator = ", ";
        }
        return reasonText;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalUnaryNode(const UnaryOpNode& unaryNode) {
        const auto operandNode = eval(unaryNode.operand);
        if (!operandNode.has_value()) {
            return std::nullopt;
        }
        const RuntimeValue unaryValue{!toBool(operandNode->value)};
        return EvaluatedNode{.value = unaryValue, .reason = "not (" + operandNode->reason + ")"};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalBinaryNode(const BinaryOpNode& binaryNode) {
        const auto leftNode = eval(binaryNode.left);
        const auto rightNode = eval(binaryNode.right);
        if (!leftNode.has_value() || !rightNode.has_value()) {
            return std::nullopt;
        }

        switch (binaryNode.op) {
        case BinaryOperator::And: {
            const bool leftBool = toBool(leftNode->value);
            const bool rightBool = toBool(rightNode->value);
            const bool combinedValue = leftBool && rightBool;
            std::string combinedReason;
            if (combinedValue) {
                if (!leftNode->reason.empty() && !rightNode->reason.empty()) {
                    combinedReason = leftNode->reason + " and " + rightNode->reason;
                } else {
                    combinedReason = leftNode->reason + rightNode->reason;
                }
            } else {
                combinedReason = leftBool ? rightNode->reason : leftNode->reason;
            }
            return EvaluatedNode{.value = RuntimeValue{combinedValue}, .reason = combinedReason};
        }
        case BinaryOperator::Or: {
            const bool leftBool = toBool(leftNode->value);
            const bool rightBool = toBool(rightNode->value);
            const bool combinedValue = leftBool || rightBool;
            std::string combinedReason;
            if (combinedValue) {
                combinedReason = leftBool ? leftNode->reason : rightNode->reason;
            } else if (!leftNode->reason.empty() && !rightNode->reason.empty()) {
                combinedReason = leftNode->reason + " or " + rightNode->reason;
            } else {
                combinedReason = leftNode->reason + rightNode->reason;
            }
            return EvaluatedNode{.value = RuntimeValue{combinedValue}, .reason = combinedReason};
        }
        case BinaryOperator::Eq:
            return EvaluatedNode{
                .value = RuntimeValue{evalEquals(leftNode->value, rightNode->value)},
                .reason = leftNode->reason + " is = " + rightNode->reason};
        case BinaryOperator::Neq:
            return EvaluatedNode{
                .value = RuntimeValue{!evalEquals(leftNode->value, rightNode->value)},
                .reason = leftNode->reason + " is != " + rightNode->reason};
        case BinaryOperator::Gt:
            return compareRel(*leftNode, *rightNode, "gt");
        case BinaryOperator::Lt:
            return compareRel(*leftNode, *rightNode, "lt");
        case BinaryOperator::Ge:
            return compareRel(*leftNode, *rightNode, "ge");
        case BinaryOperator::Le:
            return compareRel(*leftNode, *rightNode, "le");
        case BinaryOperator::Add:
        case BinaryOperator::Sub:
            return evalAddSub(*leftNode, *rightNode, binaryNode.op == BinaryOperator::Add);
        }

        errors_.emplace_back("unsupported binary operator");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalIfCallNode(const IfCallNode& ifCallNode) {
        const auto conditionNode = eval(ifCallNode.condition);
        if (!conditionNode.has_value()) {
            return std::nullopt;
        }
        if (toBool(conditionNode->value)) {
            const auto trueNode = eval(ifCallNode.trueValue);
            if (!trueNode.has_value()) {
                return std::nullopt;
            }
            return EvaluatedNode{.value = trueNode->value, .reason = "if " + conditionNode->reason + " then " + trueNode->reason};
        }

        const auto falseNode = eval(ifCallNode.falseValue);
        if (!falseNode.has_value()) {
            return std::nullopt;
        }
        return EvaluatedNode{.value = falseNode->value, .reason = "if " + conditionNode->reason + ": " + falseNode->reason};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalMapLiteralNode(const MapLiteralNode& mapNode) {
        MapRuntimeValue runtimeMap;
        for (const auto& mapEntry : mapNode.entries) {
            const auto entryNode = eval(mapEntry.value);
            if (!entryNode.has_value()) {
                return std::nullopt;
            }
            if (std::holds_alternative<MapRuntimeValue>(entryNode->value)) {
                errors_.emplace_back("map entry value must not be a map");
                return std::nullopt;
            }

            if (std::holds_alternative<std::string>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::string>(entryNode->value)});
            } else if (std::holds_alternative<double>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<double>(entryNode->value)});
            } else if (std::holds_alternative<bool>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<bool>(entryNode->value)});
            } else if (std::holds_alternative<std::chrono::system_clock::time_point>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::chrono::system_clock::time_point>(entryNode->value)});
            }
        }

        return EvaluatedNode{.value = RuntimeValue{runtimeMap}, .reason = "map literal"};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalMapCallNode(const MapCallNode& mapCallNode) {
        const auto declarationIterator = declarations_.find(mapCallNode.name);
        if (declarationIterator == declarations_.end()) {
            errors_.emplace_back("undefined map declaration: " + mapCallNode.name);
            return std::nullopt;
        }

        const auto selectorNode = eval(mapCallNode.selector);
        if (!selectorNode.has_value()) {
            return std::nullopt;
        }

        std::optional<EvaluatedNode> defaultValue;
        for (const auto& mapEntry : declarationIterator->second) {
            auto entryNode = eval(mapEntry.value);
            if (!entryNode.has_value()) {
                return std::nullopt;
            }

            if (mapEntry.key.isDefault) {
                defaultValue = *entryNode;
                continue;
            }

            if (matchesMapKey(mapEntry.key.token, selectorNode->value)) {
                return EvaluatedNode{
                    .value = entryNode->value,
                    .reason = selectorNode->reason + " mapped to " + valueToString(entryNode->value)};
            }
        }

        if (defaultValue.has_value()) {
            return EvaluatedNode{
                .value = defaultValue->value,
                .reason = selectorNode->reason + " mapped to " + valueToString(defaultValue->value)};
        }

        errors_.emplace_back("map call has no matching key and no default: " + mapCallNode.name);
        return std::nullopt;
    }

    [[nodiscard]] static bool matchesMapKey(const std::string& keyToken, const RuntimeValue& selectorValue) {
        double keyNumber = 0.0;
        if (parseDouble(keyToken, &keyNumber) && std::holds_alternative<double>(selectorValue)) {
            return std::fabs(std::get<double>(selectorValue) - keyNumber) < k_numeric_epsilon;
        }

        std::string normalizedKey = keyToken;
        if (normalizedKey.size() >= 2U && (normalizedKey.front() == '\'' || normalizedKey.front() == '"')
            && normalizedKey.back() == normalizedKey.front()) {
            normalizedKey = normalizedKey.substr(1U, normalizedKey.size() - 2U);
        }

        return normalizedKey == valueToString(selectorValue);
    }

    [[nodiscard]] std::optional<EvaluatedNode> compareRel(
        const EvaluatedNode& leftNode,
        const EvaluatedNode& rightNode,
        const std::string& relationOperator) {
        auto makeCompareNode = [&](const bool compareValue, const std::string& operatorText) {
            return EvaluatedNode{
                .value = RuntimeValue{compareValue},
                .reason = leftNode.reason + " is " + operatorText + " " + rightNode.reason};
        };

        const RuntimeValue& leftValue = leftNode.value;
        const RuntimeValue& rightValue = rightNode.value;
        if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
            const double leftNumber = std::get<double>(leftValue);
            const double rightNumber = std::get<double>(rightValue);
            if (relationOperator == "gt") {
                return makeCompareNode(leftNumber > rightNumber, ">");
            }
            if (relationOperator == "lt") {
                return makeCompareNode(leftNumber < rightNumber, "<");
            }
            if (relationOperator == "ge") {
                return makeCompareNode(leftNumber >= rightNumber, ">=");
            }
            return makeCompareNode(leftNumber <= rightNumber, "<=");
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        const auto rightTime = tryTimeOfDay(rightValue);
        if (leftTime.has_value() && rightTime.has_value()) {
            if (relationOperator == "gt") {
                return makeCompareNode(*leftTime > *rightTime, ">");
            }
            if (relationOperator == "lt") {
                return makeCompareNode(*leftTime < *rightTime, "<");
            }
            if (relationOperator == "ge") {
                return makeCompareNode(*leftTime >= *rightTime, ">=");
            }
            return makeCompareNode(*leftTime <= *rightTime, "<=");
        }

        errors_.emplace_back("invalid operands for relational comparison");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalAddSub(
        const EvaluatedNode& leftNode,
        const EvaluatedNode& rightNode,
        const bool isAddOperation) {
        const RuntimeValue& leftValue = leftNode.value;
        const RuntimeValue& rightValue = rightNode.value;
        const std::string operatorText = isAddOperation ? "+" : "-";
        if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
            const double leftNumber = std::get<double>(leftValue);
            const double rightNumber = std::get<double>(rightValue);
            const RuntimeValue resultValue{isAddOperation ? leftNumber + rightNumber : leftNumber - rightNumber};
            return EvaluatedNode{
                .value = resultValue,
                .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        if (leftTime.has_value() && std::holds_alternative<double>(rightValue)) {
            const auto deltaMinutes = std::llround(std::get<double>(rightValue));
            const auto deltaDuration = std::chrono::minutes{deltaMinutes};
            const auto resultTime = isAddOperation ? *leftTime + deltaDuration : *leftTime - deltaDuration;
            if (std::holds_alternative<std::chrono::system_clock::time_point>(leftValue)) {
                const auto dayStart = localDayStart(std::get<std::chrono::system_clock::time_point>(leftValue));
                if (!dayStart.has_value()) {
                    errors_.emplace_back("failed to derive local day start");
                    return std::nullopt;
                }
                const RuntimeValue resultValue{*dayStart + resultTime};
                return EvaluatedNode{
                    .value = resultValue,
                    .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
            }
            const RuntimeValue resultValue{formatTimeText(resultTime)};
            return EvaluatedNode{
                .value = resultValue,
                .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
        }

        errors_.emplace_back("invalid operands for arithmetic operation");
        return std::nullopt;
    }

    const FieldScriptAst& script_;
    const ExternalVariableMap& variables_;
    std::map<std::string, std::vector<MapEntryAst>> declarations_;
    std::set<std::string> usedVariables_;
    std::set<std::string> missingVariables_;
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
