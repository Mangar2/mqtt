#include "yaha/automation/single_rule_processor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>

#include "yaha/automation/expression_parser.h"

namespace yaha {
namespace {

constexpr double k_numeric_bool_epsilon{1e-12};
constexpr double k_qos_at_most_once{0.0};
constexpr double k_qos_at_least_once{1.0};
constexpr double k_qos_exactly_once{2.0};

struct ResolvedRuleOutput {
    std::string topic;
    const RuleTreeNode* valueNode{nullptr};
};

void appendTrace(std::vector<std::string>* traceEntries, const std::string& traceText) {
    if (traceEntries == nullptr) {
        return;
    }
    traceEntries->push_back(traceText);
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

[[nodiscard]] bool handleStringTopic(
    const RuleTreeNode& topicNode,
    const RuleTreeNode* sharedValueNode,
    std::vector<ResolvedRuleOutput>* outputs,
    std::vector<std::string>* errors) {
    if (sharedValueNode == nullptr) {
        errors->emplace_back("rule requires value field");
        return false;
    }
    const std::string& topicText = topicNode.asString();
    if (topicText.empty()) {
        errors->emplace_back("rule topic must not be empty");
        return false;
    }
    outputs->push_back(ResolvedRuleOutput{.topic = topicText, .valueNode = sharedValueNode});
    return true;
}

[[nodiscard]] bool handleArrayTopic(
    const RuleTreeNode& topicNode,
    const RuleTreeNode* sharedValueNode,
    std::vector<ResolvedRuleOutput>* outputs,
    std::vector<std::string>* errors) {
    if (sharedValueNode == nullptr) {
        errors->emplace_back("rule requires value field");
        return false;
    }

    const auto& topics = topicNode.asArray();
    if (topics.empty()) {
        errors->emplace_back("rule topic array must not be empty");
        return false;
    }

    if (!std::ranges::all_of(topics, [](const RuleTreeNode& entry) { return entry.isString(); })) {
        errors->emplace_back("rule topic array entries must be strings");
        return false;
    }

    for (const auto& topicEntry : topics) {
        outputs->push_back(ResolvedRuleOutput{.topic = topicEntry.asString(), .valueNode = sharedValueNode});
    }
    return true;
}

[[nodiscard]] bool handleObjectTopic(
    const RuleTreeNode& topicNode,
    std::vector<ResolvedRuleOutput>* outputs,
    std::vector<std::string>* errors) {
    const auto& topicObject = topicNode.asObject();
    if (topicObject.empty()) {
        errors->emplace_back("rule topic map must not be empty");
        return false;
    }

    for (const auto& [topicText, valueNode] : topicObject) {
        if (topicText.empty()) {
            errors->emplace_back("rule topic must not be empty");
            return false;
        }
        outputs->push_back(ResolvedRuleOutput{.topic = topicText, .valueNode = &valueNode});
    }
    return true;
}

[[nodiscard]] bool collectResolvedOutputs(
    const RuleTreeNode& ruleNode,
    std::vector<ResolvedRuleOutput>* outputs,
    std::vector<std::string>* errors) {
    if (!ruleNode.isObject()) {
        errors->emplace_back("rule node must be an object");
        return false;
    }

    const auto& ruleObject = ruleNode.asObject();
    const auto topicIterator = ruleObject.find("topic");
    if (topicIterator == ruleObject.end()) {
        errors->emplace_back("rule requires topic field");
        return false;
    }

    const RuleTreeNode& topicNode = topicIterator->second;
    const auto valueIterator = ruleObject.find("value");
    const RuleTreeNode* sharedValueNode = valueIterator != ruleObject.end() ? &valueIterator->second : nullptr;

    if (topicNode.isString()) {
        return handleStringTopic(topicNode, sharedValueNode, outputs, errors);
    }

    if (topicNode.isArray()) {
        return handleArrayTopic(topicNode, sharedValueNode, outputs, errors);
    }

    if (topicNode.isObject()) {
        return handleObjectTopic(topicNode, outputs, errors);
    }

    errors->emplace_back("rule topic must be string, array, or object");
    return false;
}

[[nodiscard]] bool hasExecutableProgram(const RuleTreeNode& ruleNode) {
    if (!ruleNode.isObject()) {
        return false;
    }

    const auto& ruleObject = ruleNode.asObject();
    if (ruleObject.contains("check")) {
        return true;
    }

    const auto topicIterator = ruleObject.find("topic");
    if (topicIterator != ruleObject.end() && topicIterator->second.isObject()) {
        for (const auto& [topicText, valueNode] : topicIterator->second.asObject()) {
            (void)topicText;
            if (valueNode.isString()) {
                return true;
            }
        }
    }

    const auto valueIterator = ruleObject.find("value");
    return valueIterator != ruleObject.end() && valueIterator->second.isString();
}

[[nodiscard]] std::string buildSummaryReason(
    const std::string& ruleIdentifier,
    const std::string& topic,
    const std::vector<std::string>& traceEntries) {
    constexpr std::string_view ruleIdentifierPrefix = "rule-evaluation:rule=";
    constexpr std::string_view valueReasonPrefix = "rule-evaluation:value reason=";
    constexpr std::string_view checkReasonPrefix = "rule-evaluation:check reason=";

    std::string tracedRuleIdentifier;
    std::string valueReason;
    std::string checkReason;

    for (const auto& entry : traceEntries) {
        if (tracedRuleIdentifier.empty() && entry.starts_with(ruleIdentifierPrefix)) {
            tracedRuleIdentifier = entry.substr(ruleIdentifierPrefix.size());
        } else if (valueReason.empty() && entry.starts_with(valueReasonPrefix)) {
            valueReason = entry.substr(valueReasonPrefix.size());
        } else if (checkReason.empty() && entry.starts_with(checkReasonPrefix)) {
            checkReason = entry.substr(checkReasonPrefix.size());
        }
    }

    std::string ruleSummaryIdentifier;
    if (!tracedRuleIdentifier.empty()) {
        ruleSummaryIdentifier = tracedRuleIdentifier;
    } else if (!ruleIdentifier.empty()) {
        ruleSummaryIdentifier = ruleIdentifier;
    } else {
        ruleSummaryIdentifier = topic;
    }

    if (ruleSummaryIdentifier.empty()) {
        return {};
    }

    std::string summary = "Rule: " + ruleSummaryIdentifier;
    if (!checkReason.empty()) {
        summary += ", check: " + checkReason;
    }
    if (!valueReason.empty()) {
        summary += ", value: " + valueReason;
    }
    return summary;
}

[[nodiscard]] std::string normalizeRuleIdentifier(const std::string& ruleIdentifier) {
    if (ruleIdentifier.empty()) {
        return {};
    }

    constexpr std::string_view rulesSegment{"/rules/"};
    const std::size_t rulesSegmentPos = ruleIdentifier.find(rulesSegment);
    if (rulesSegmentPos != std::string::npos) {
        return ruleIdentifier.substr(rulesSegmentPos + rulesSegment.size());
    }

    constexpr std::string_view rulesPrefix{"rules/"};
    if (ruleIdentifier.starts_with(rulesPrefix)) {
        return ruleIdentifier.substr(rulesPrefix.size());
    }

    return ruleIdentifier;
}

[[nodiscard]] std::string resolveRuleIdentifier(
    const RuleTreeNode& ruleNode,
    const std::string& explicitRuleIdentifier) {
    if (!explicitRuleIdentifier.empty()) {
        return normalizeRuleIdentifier(explicitRuleIdentifier);
    }

    if (!ruleNode.isObject()) {
        return {};
    }

    const auto& ruleObject = ruleNode.asObject();
    const auto nameIterator = ruleObject.find("name");
    if (nameIterator == ruleObject.end() || !nameIterator->second.isString()) {
        return {};
    }

    return normalizeRuleIdentifier(nameIterator->second.asString());
}

} // namespace

SingleRuleProcessingResult SingleRuleProcessor::process(
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables,
    const std::string& ruleIdentifier) {
    std::vector<std::string> traceEntries;
    SingleRuleProcessingResult result = processWithTrace(
        ruleNode,
        variables,
        &traceEntries,
        ruleIdentifier);

    if (result.success && result.triggered && !result.messages.empty() && hasExecutableProgram(ruleNode)) {
        const std::string resolvedRuleIdentifier = resolveRuleIdentifier(ruleNode, ruleIdentifier);
        for (auto& message : result.messages) {
            const std::string summaryReason = buildSummaryReason(
                resolvedRuleIdentifier,
                message.topic(),
                traceEntries);
            if (!summaryReason.empty()) {
                message.addReason(summaryReason);
            }
        }

        if (!result.messages.empty()) {
            result.message = result.messages.front();
        }
    }

    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
SingleRuleProcessingResult SingleRuleProcessor::processWithTrace(
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables,
    std::vector<std::string>* traceEntries,
    const std::string& ruleIdentifier) {
    SingleRuleProcessingResult result;
    appendTrace(traceEntries, "rule-evaluation:start");

    const std::string resolvedRuleIdentifier = resolveRuleIdentifier(ruleNode, ruleIdentifier);
    if (!resolvedRuleIdentifier.empty()) {
        appendTrace(traceEntries, "rule-evaluation:rule=" + resolvedRuleIdentifier);
    }

    std::vector<ResolvedRuleOutput> outputs;
    if (!collectResolvedOutputs(ruleNode, &outputs, &result.errors)) {
        if (result.errors.empty()) {
            result.errors.emplace_back("rule requires topic field");
        }
        appendTrace(traceEntries, "rule-evaluation:error " + result.errors.back());
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:rule object shape ok");

    appendTrace(traceEntries, "rule-evaluation:topic=" + outputs.front().topic);
    for (std::size_t index = 1U; index < outputs.size(); ++index) {
        appendTrace(traceEntries, "rule-evaluation:topic[" + std::to_string(index) + "]=" + outputs[index].topic);
    }
    appendTrace(traceEntries, "rule-evaluation:shape required fields validated (topic)");

    const auto& ruleObject = ruleNode.asObject();

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
            return result;
        }

        result.usedVariables.insert(checkResult.usedVariables.begin(), checkResult.usedVariables.end());
        isTriggered = asBooleanValue(checkResult.value);
        appendTrace(traceEntries,
                    "rule-evaluation:check result=" + evaluationValueToTraceText(checkResult.value));
        if (!checkResult.reason.empty()) {
            appendTrace(traceEntries, "rule-evaluation:check reason=" + checkResult.reason);
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

    if (!ruleObject.at("topic").isObject() && !ruleObject.contains("value")) {
        result.errors.emplace_back("rule requires value field");
        appendTrace(traceEntries, "rule-evaluation:error rule requires value field");
        return result;
    }

    if (!ruleObject.at("topic").isObject() || ruleObject.contains("value")) {
        appendTrace(traceEntries, "rule-evaluation:shape required fields validated (value)");
    } else {
        appendTrace(traceEntries, "rule-evaluation:shape required fields validated (topic values)");
    }

    std::vector<Message> messages;
    messages.reserve(outputs.size());

    for (const auto& output : outputs) {
        Value messageValue;
        const RuleTreeNode& valueNode = *output.valueNode;
        if (valueNode.isString()) {
            if (valueNode.asString().empty()) {
                appendTrace(traceEntries, "rule-evaluation:value literal string");
                messageValue = std::string{};
            } else {
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
            }
        } else if (std::holds_alternative<double>(valueNode.value)) {
            messageValue = std::get<double>(valueNode.value);
            appendTrace(traceEntries, "rule-evaluation:value literal number");
        } else if (std::holds_alternative<bool>(valueNode.value)) {
            messageValue = std::get<bool>(valueNode.value) ? std::string{"true"} : std::string{"false"};
            appendTrace(traceEntries, "rule-evaluation:value literal bool");
        } else {
            result.errors.emplace_back("topic value must be expression string, number, or bool");
            appendTrace(traceEntries, "rule-evaluation:error topic value must be expression string, number, or bool");
            return result;
        }

        messages.emplace_back(output.topic, std::move(messageValue), Qos::AtLeastOnce);
    }

    const Qos qosValue = parseQos(ruleNode, &result.errors);
    if (!result.errors.empty()) {
        appendTrace(traceEntries, "rule-evaluation:error invalid qos");
        return result;
    }

    appendTrace(traceEntries, "rule-evaluation:qos=" + qosToTraceText(qosValue));

    for (auto& message : messages) {
        message = Message{message.topic(), message.value(), qosValue};
    }

    result.messages = std::move(messages);
    if (!result.messages.empty()) {
        result.message = result.messages.front();
    }
    result.success = true;
    appendTrace(traceEntries, "rule-evaluation:finish triggered message-ready");
    return result;
}

} // namespace yaha
