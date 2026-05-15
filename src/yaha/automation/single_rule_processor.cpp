#include "yaha/automation/single_rule_processor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include "yaha/automation/expression_parser.h"

namespace yaha {
namespace {

constexpr double k_numeric_bool_epsilon{1e-12};
constexpr double k_qos_at_most_once{0.0};
constexpr double k_qos_at_least_once{1.0};
constexpr double k_qos_exactly_once{2.0};

void appendTrace(std::vector<std::string>* traceEntries, const std::string& traceText) {
    if (traceEntries == nullptr) {
        return;
    }
    traceEntries->push_back(traceText);
}

[[nodiscard]] std::string variableValueToTraceText(const ExpressionEvaluator::Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return "string:" + std::get<std::string>(value);
    }
    if (std::holds_alternative<double>(value)) {
        return "number:" + std::to_string(std::get<double>(value));
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "bool:true" : "bool:false";
    }
    // time_point — format as ISO-like string
    const auto timePointValue = std::get<std::chrono::system_clock::time_point>(value);
    const std::time_t timeT = std::chrono::system_clock::to_time_t(timePointValue);
    std::ostringstream stream;
    stream << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
    return "time:" + stream.str();
}

[[nodiscard]] std::string evaluationValueToTraceText(const ExpressionEvaluationResult::Value& value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "bool:true" : "bool:false";
    }
    if (std::holds_alternative<double>(value)) {
        return "number:" + std::to_string(std::get<double>(value));
    }
    if (std::holds_alternative<std::string>(value)) {
        return "string:" + std::get<std::string>(value);
    }
    return "time";
}

[[nodiscard]] std::string qosToTraceText(const Qos qosValue) {
    switch (qosValue) {
    case Qos::AtMostOnce:
        return "0";
    case Qos::AtLeastOnce:
        return "1";
    case Qos::ExactlyOnce:
        return "2";
    }

    return "unknown";
}

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

void appendVariableTraceEntry(std::vector<std::string>* traceEntries,
                              const std::string& variableName,
                              const std::string& variableValueText) {
    std::string traceText{"rule-evaluation:var "};
    traceText.append(variableName);
    traceText.push_back('=');
    traceText.append(variableValueText);
    appendTrace(traceEntries, traceText);
}

} // namespace

SingleRuleProcessingResult SingleRuleProcessor::process(
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables) {
    return processWithTrace(ruleNode, variables, nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SingleRuleProcessingResult SingleRuleProcessor::processWithTrace(
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables,
    std::vector<std::string>* traceEntries) {
    SingleRuleProcessingResult result;
    appendTrace(traceEntries, "rule-evaluation:start");

    if (!ruleNode.isObject()) {
        result.errors.emplace_back("rule node must be an object");
        appendTrace(traceEntries, "rule-evaluation:error rule node must be an object");
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:rule object shape ok");

    const auto& ruleObject = ruleNode.asObject();
    if (!ruleObject.contains("topic") || !ruleObject.at("topic").isString()) {
        result.errors.emplace_back("rule requires string topic");
        appendTrace(traceEntries, "rule-evaluation:error rule requires string topic");
        return result;
    }

    const std::string topicName = ruleObject.at("topic").asString();
    if (topicName.empty()) {
        result.errors.emplace_back("rule topic must not be empty");
        appendTrace(traceEntries, "rule-evaluation:error rule topic must not be empty");
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:topic=" + topicName);
    appendTrace(traceEntries, "rule-evaluation:shape required fields validated (topic)");

    bool isTriggered = true;
    if (ruleObject.contains("check")) {
        const RuleTreeNode& checkNode = ruleObject.at("check");
        if (!checkNode.isString()) {
            result.errors.emplace_back("check must be expression string");
            appendTrace(traceEntries, "rule-evaluation:error check must be expression string");
            return result;
        }

        appendTrace(traceEntries, "rule-evaluation:check expr=" + checkNode.asString());

        ExpressionEvaluationResult checkResult;
        if (!evaluateScript("check", checkNode.asString(), variables, &checkResult, &result.errors)) {
            appendTrace(traceEntries, "rule-evaluation:error check evaluation failed");
            for (const auto& varName : checkResult.usedVariables) {
                const auto varIt = variables.find(varName);
                const std::string varVal = (varIt != variables.end())
                    ? variableValueToTraceText(varIt->second)
                    : "undefined";
                appendVariableTraceEntry(traceEntries, varName, varVal);
            }
            return result;
        }

        result.usedVariables.insert(checkResult.usedVariables.begin(), checkResult.usedVariables.end());
        isTriggered = asBooleanValue(checkResult.value);
        appendTrace(traceEntries,
                    "rule-evaluation:check result=" + evaluationValueToTraceText(checkResult.value));
        if (!checkResult.reason.empty()) {
            appendTrace(traceEntries, "rule-evaluation:check reason=" + checkResult.reason);
        }
        for (const auto& varName : checkResult.usedVariables) {
            const auto varIt = variables.find(varName);
            const std::string varVal = (varIt != variables.end())
                ? variableValueToTraceText(varIt->second)
                : "undefined";
            appendVariableTraceEntry(traceEntries, varName, varVal);
        }
        appendTrace(traceEntries,
                    std::string{"rule-evaluation:check decision="}
                        + (isTriggered ? "trigger" : "skip"));
    } else {
        appendTrace(traceEntries, "rule-evaluation:check missing default=true");
    }

    result.triggered = isTriggered;
    if (!isTriggered) {
        result.success = true;
        appendTrace(traceEntries, "rule-evaluation:event-gates=not_evaluated (rule not triggered by check)");
        appendTrace(traceEntries, "rule-evaluation:finish not_triggered");
        return result;
    }

    const bool hasEventGates = ruleObject.contains("allOf")
        || ruleObject.contains("anyOf")
        || ruleObject.contains("noneOf")
        || ruleObject.contains("allow");
    if (hasEventGates) {
        appendTrace(traceEntries,
                    "rule-evaluation:event-gates=declared (allOf/anyOf/noneOf/allow), "
                    "single-rule debug path has no event history context");
    } else {
        appendTrace(traceEntries, "rule-evaluation:event-gates=none");
    }

    if (!ruleObject.contains("value")) {
        result.errors.emplace_back("rule requires value field");
        appendTrace(traceEntries, "rule-evaluation:error rule requires value field");
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:shape required fields validated (value)");

    Value messageValue;
    const RuleTreeNode& valueNode = ruleObject.at("value");
    if (valueNode.isString()) {
        appendTrace(traceEntries, "rule-evaluation:value expr=" + valueNode.asString());
        ExpressionEvaluationResult valueResult;
        if (!evaluateScript("value", valueNode.asString(), variables, &valueResult, &result.errors)) {
            appendTrace(traceEntries, "rule-evaluation:error value evaluation failed");
            return result;
        }

        result.usedVariables.insert(valueResult.usedVariables.begin(), valueResult.usedVariables.end());
        if (!mapEvaluationValueToMessageValue(valueResult.value, &messageValue, &result.errors)) {
            appendTrace(traceEntries, "rule-evaluation:error value mapping failed");
            return result;
        }

        appendTrace(traceEntries,
                    "rule-evaluation:value result=" + evaluationValueToTraceText(valueResult.value));
        if (!valueResult.reason.empty()) {
            appendTrace(traceEntries, "rule-evaluation:value reason=" + valueResult.reason);
        }
        for (const auto& varName : valueResult.usedVariables) {
            const auto varIt = variables.find(varName);
            const std::string varVal = (varIt != variables.end())
                ? variableValueToTraceText(varIt->second)
                : "undefined";
            appendVariableTraceEntry(traceEntries, varName, varVal);
        }
    } else if (std::holds_alternative<double>(valueNode.value)) {
        messageValue = std::get<double>(valueNode.value);
        appendTrace(traceEntries, "rule-evaluation:value literal number");
    } else if (std::holds_alternative<bool>(valueNode.value)) {
        messageValue = std::get<bool>(valueNode.value) ? std::string{"true"} : std::string{"false"};
        appendTrace(traceEntries, "rule-evaluation:value literal bool");
    } else {
        result.errors.emplace_back("value must be expression string, number, or bool");
        appendTrace(traceEntries, "rule-evaluation:error value must be expression string, number, or bool");
        return result;
    }

    const Qos qosValue = parseQos(ruleNode, &result.errors);
    if (!result.errors.empty()) {
        appendTrace(traceEntries, "rule-evaluation:error invalid qos");
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:qos=" + qosToTraceText(qosValue));

    result.message = Message{topicName, std::move(messageValue), qosValue};
    result.success = true;
    appendTrace(traceEntries, "rule-evaluation:finish triggered message-ready");
    return result;
}

} // namespace yaha
