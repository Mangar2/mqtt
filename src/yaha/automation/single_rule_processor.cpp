#include "yaha/automation/single_rule_processor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

#include "yaha/automation/expression_parser.h"

namespace yaha {
namespace {

constexpr double k_numeric_bool_epsilon{1e-12};
constexpr double k_qos_at_most_once{0.0};
constexpr double k_qos_at_least_once{1.0};
constexpr double k_qos_exactly_once{2.0};

[[nodiscard]] std::string trimText(const std::string& textValue) {
    std::size_t beginIndex = 0U;
    while (beginIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    std::size_t endIndex = textValue.size();
    while (endIndex > beginIndex
        && std::isspace(static_cast<unsigned char>(textValue[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return textValue.substr(beginIndex, endIndex - beginIndex);
}

[[nodiscard]] std::string toLowerText(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](const char currentChar) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(currentChar)));
    });
    return textValue;
}

[[nodiscard]] bool asBooleanValue(const ExpressionEvaluationResult::Value& value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    if (std::holds_alternative<double>(value)) {
        return std::fabs(std::get<double>(value)) > k_numeric_bool_epsilon;
    }
    if (std::holds_alternative<std::string>(value)) {
        const std::string normalized = toLowerText(trimText(std::get<std::string>(value)));
        return !(normalized.empty() || normalized == "0" || normalized == "false"
            || normalized == "off" || normalized == "no");
    }
    return true;
}

[[nodiscard]] bool mapEvaluationValueToMessageValue(
    const ExpressionEvaluationResult::Value& value,
    Value* outValue,
    std::vector<std::string>* errors) {
    if (std::holds_alternative<std::string>(value)) {
        *outValue = std::get<std::string>(value);
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        *outValue = std::get<double>(value);
        return true;
    }
    if (std::holds_alternative<bool>(value)) {
        *outValue = std::get<bool>(value) ? std::string{"true"} : std::string{"false"};
        return true;
    }

    errors->emplace_back("rule value expression produced unsupported time value");
    return false;
}

[[nodiscard]] bool appendExpressionErrors(
    const ExpressionParseResult& parseResult,
    std::vector<std::string>* errors,
    const std::string& fieldName) {
    for (const auto& parseError : parseResult.errors) {
        errors->emplace_back(fieldName + ": " + parseError.message);
    }
    return parseResult.success;
}

[[nodiscard]] bool evaluateScript(
    const std::string& fieldName,
    const std::string& script,
    const ExpressionEvaluator::VariableMap& variables,
    ExpressionEvaluationResult* outResult,
    std::vector<std::string>* errors) {
    const ExpressionParseResult parseResult = ExpressionParser::parse(script);
    if (!appendExpressionErrors(parseResult, errors, fieldName)) {
        return false;
    }

    *outResult = ExpressionEvaluator::evaluate(parseResult.ast, variables);
    if (!outResult->success) {
        for (const auto& errorText : outResult->errors) {
            std::string prefixedError = fieldName;
            prefixedError.append(": ");
            prefixedError.append(errorText);
            errors->push_back(std::move(prefixedError));
        }
        return false;
    }

    return true;
}

[[nodiscard]] Qos parseQos(const RuleTreeNode& ruleNode, std::vector<std::string>* errors) {
    if (!ruleNode.asObject().contains("qos")) {
        return Qos::AtLeastOnce;
    }

    const RuleTreeNode& qosNode = ruleNode.asObject().at("qos");
    if (!std::holds_alternative<double>(qosNode.value)) {
        errors->emplace_back("qos must be numeric 0..2");
        return Qos::AtLeastOnce;
    }

    const double qosNumber = std::get<double>(qosNode.value);
    if (qosNumber == k_qos_at_most_once) {
        return Qos::AtMostOnce;
    }
    if (qosNumber == k_qos_at_least_once) {
        return Qos::AtLeastOnce;
    }
    if (qosNumber == k_qos_exactly_once) {
        return Qos::ExactlyOnce;
    }

    errors->emplace_back("qos must be 0, 1, or 2");
    return Qos::AtLeastOnce;
}

} // namespace

SingleRuleProcessingResult SingleRuleProcessor::process(
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables) {
    SingleRuleProcessingResult result;

    if (!ruleNode.isObject()) {
        result.errors.emplace_back("rule node must be an object");
        return result;
    }

    const auto& ruleObject = ruleNode.asObject();
    if (!ruleObject.contains("topic") || !ruleObject.at("topic").isString()) {
        result.errors.emplace_back("rule requires string topic");
        return result;
    }

    const std::string topicName = ruleObject.at("topic").asString();
    if (topicName.empty()) {
        result.errors.emplace_back("rule topic must not be empty");
        return result;
    }

    bool isTriggered = true;
    if (ruleObject.contains("check")) {
        const RuleTreeNode& checkNode = ruleObject.at("check");
        if (!checkNode.isString()) {
            result.errors.emplace_back("check must be expression string");
            return result;
        }

        ExpressionEvaluationResult checkResult;
        if (!evaluateScript("check", checkNode.asString(), variables, &checkResult, &result.errors)) {
            return result;
        }

        result.usedVariables.insert(checkResult.usedVariables.begin(), checkResult.usedVariables.end());
        isTriggered = asBooleanValue(checkResult.value);
    }

    result.triggered = isTriggered;
    if (!isTriggered) {
        result.success = true;
        return result;
    }

    if (!ruleObject.contains("value")) {
        result.errors.emplace_back("rule requires value field");
        return result;
    }

    Value messageValue;
    const RuleTreeNode& valueNode = ruleObject.at("value");
    if (valueNode.isString()) {
        ExpressionEvaluationResult valueResult;
        if (!evaluateScript("value", valueNode.asString(), variables, &valueResult, &result.errors)) {
            return result;
        }

        result.usedVariables.insert(valueResult.usedVariables.begin(), valueResult.usedVariables.end());
        if (!mapEvaluationValueToMessageValue(valueResult.value, &messageValue, &result.errors)) {
            return result;
        }
    } else if (std::holds_alternative<double>(valueNode.value)) {
        messageValue = std::get<double>(valueNode.value);
    } else if (std::holds_alternative<bool>(valueNode.value)) {
        messageValue = std::get<bool>(valueNode.value) ? std::string{"true"} : std::string{"false"};
    } else {
        result.errors.emplace_back("value must be expression string, number, or bool");
        return result;
    }

    const Qos qosValue = parseQos(ruleNode, &result.errors);
    if (!result.errors.empty()) {
        return result;
    }

    result.message = Message{topicName, std::move(messageValue), qosValue};
    result.success = true;
    return result;
}

} // namespace yaha
