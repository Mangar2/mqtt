#include "yaha/automation_client_rule_runtime/rule_runtime_engine.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "yaha/automation/single_rule_processor.h"
#include "yaha/automation_client_rule_runtime/rule_delivery_control.h"
#include "yaha/automation_client_rule_runtime/rule_event_gate.h"
#include "yaha/automation_client_rule_runtime/rule_event_gate_trace.h"
#include "yaha/automation_client_rule_runtime/rule_field_access.h"
#include "yaha/automation_client_rule_runtime/rule_time_gate.h"
#include "yaha/automation_client_rule_runtime/rule_topic_filter.h"

namespace yaha {
namespace {

constexpr std::size_t k_max_motion_events{100U};
constexpr std::size_t k_motion_trim_count{20U};

struct RuntimeRuleEvaluationOutcome {
    bool success{true};
    bool triggered{false};
    std::vector<Message> candidateMessages;
    std::vector<Message> emittedMessages;
    std::set<std::string> usedVariables;
    std::vector<std::string> errors;
    std::optional<std::string> eventTriggerReason;
};

void handleGateMiss(
    const bool retainDeliveryStateOnGateMiss,
    const std::string& pathText,
    RuleRuntimeDeliveryState* deliveryState,
    std::vector<std::string>* traceEntries,
    const std::string& traceText) {
    appendRuntimeTrace(traceEntries, traceText);
    if (!retainDeliveryStateOnGateMiss) {
        clearRuleDeliveryState(pathText, deliveryState);
    }
}

void handleGateErrors(
    RuntimeRuleEvaluationOutcome* outcome,
    const bool retainDeliveryStateOnGateMiss,
    const std::string& pathText,
    RuleRuntimeDeliveryState* deliveryState,
    std::vector<std::string>* traceEntries,
    const std::vector<std::string>& errorTexts) {
    outcome->success = false;
    for (const auto& errorText : errorTexts) {
        outcome->errors.push_back(errorText);
        appendRuntimeTrace(traceEntries, "error: " + errorText);
    }
    if (!retainDeliveryStateOnGateMiss) {
        clearRuleDeliveryState(pathText, deliveryState);
    }
}

void appendPathError(
    RuleRuntimeProcessingResult* result,
    const std::string& pathText,
    const std::string& errorText) {
    result->success = false;
    std::string formattedError = pathText;
    formattedError.append(": ");
    formattedError.append(errorText);
    result->errors.push_back(std::move(formattedError));
}

[[nodiscard]] RuntimeRuleEvaluationOutcome evaluateRuleNodeRuntime(
    const RuleTreeNode& node,
    const std::string& pathText,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState,
    std::vector<std::string>* traceEntries) {
    RuntimeRuleEvaluationOutcome outcome{};

    if (!node.isObject()) {
        outcome.success = false;
        outcome.errors.emplace_back("rule node must be an object");
        appendRuntimeTrace(traceEntries, "error: rule node must be an object");
        return outcome;
    }

    const auto& ruleObject = node.asObject();
    const bool retainDeliveryStateOnGateMiss = shouldRetainDeliveryStateOnGateMiss(ruleObject);
    const bool hasWeekdaysField = ruleObject.contains("weekdays");
    const bool hasTimeField = ruleObject.contains("time");

    if (!readActiveFlag(ruleObject)) {
        handleGateMiss(
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            "active: failed (active == false)");
        return outcome;
    }
    appendRuntimeTrace(traceEntries, "active: passed (active == true)");

    if (!evaluateWeekdayGate(ruleObject, evaluationTime)) {
        handleGateMiss(
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            "weekdays: failed (evaluation weekday not in configured weekdays)");
        return outcome;
    }
    if (hasWeekdaysField) {
        appendRuntimeTrace(traceEntries, "weekdays: passed (evaluation weekday in configured weekdays)");
    }

    std::vector<std::string> gateErrors{};
    const bool timeGatePass = evaluateTimeWindowGate(ruleObject, variables, evaluationTime, &gateErrors);
    if (!gateErrors.empty()) {
        handleGateErrors(
            &outcome,
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            gateErrors);
        return outcome;
    }
    if (!timeGatePass) {
        handleGateMiss(
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            "time: failed (evaluation time outside configured window)");
        return outcome;
    }
    if (hasTimeField) {
        appendRuntimeTrace(traceEntries, "time: passed (evaluation time in configured window)");
    }

    appendConfiguredEventGateTraceEntries(ruleObject, *eventState, evaluationTime, traceEntries);

    if (!evaluateEventGates(ruleObject, *eventState, evaluationTime)) {
        handleGateMiss(
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            "events: failed (event gate conditions not met)");
        return outcome;
    }

    outcome.eventTriggerReason = buildEventTriggerReason(ruleObject, *eventState, evaluationTime);

    const SingleRuleProcessingResult singleResult = SingleRuleProcessor::process(node, variables, pathText);
    outcome.usedVariables = singleResult.usedVariables;

    if (!singleResult.success) {
        handleGateErrors(
            &outcome,
            retainDeliveryStateOnGateMiss,
            pathText,
            deliveryState,
            traceEntries,
            singleResult.errors);
        return outcome;
    }

    if (!singleResult.triggered || singleResult.messages.empty()) {
        if (!retainDeliveryStateOnGateMiss) {
            clearRuleDeliveryState(pathText, deliveryState);
        }
        return outcome;
    }

    outcome.triggered = true;
    outcome.candidateMessages = singleResult.messages;
    outcome.emittedMessages = applyDeliveryControls(
        pathText,
        ruleObject,
        outcome.candidateMessages,
        evaluationTime,
        deliveryState);
    appendRuntimeTrace(
        traceEntries,
        "delivery: candidates="
            + std::to_string(outcome.candidateMessages.size())
            + " delivered=" + std::to_string(outcome.emittedMessages.size()));
    return outcome;
}

void processRuleNode(
    const RuleTreeNode& node,
    const std::string& pathText,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState,
    RuleRuntimeProcessingResult* result) {
    result->processedRules += 1U;
    const RuntimeRuleEvaluationOutcome outcome = evaluateRuleNodeRuntime(
        node,
        pathText,
        variables,
        evaluationTime,
        eventState,
        deliveryState,
        nullptr);
    result->usedVariables.insert(outcome.usedVariables.begin(), outcome.usedVariables.end());

    if (!outcome.success) {
        for (const auto& errorText : outcome.errors) {
            appendPathError(result, pathText, errorText);
        }
        return;
    }

    if (!outcome.triggered || outcome.candidateMessages.empty()) {
        return;
    }

    result->triggeredRules += 1U;
    for (auto emittedMessage : outcome.emittedMessages) {
        if (outcome.eventTriggerReason.has_value()) {
            emittedMessage.addReason(*outcome.eventTriggerReason);
        }
        result->messages.push_back(emittedMessage.clone());
    }
}

void processRulesRecursively(
    const RuleTreeNode& node,
    const std::string& pathText,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState,
    RuleRuntimeProcessingResult* result) {
    if (isRuleNode(node)) {
        processRuleNode(
            node,
            pathText,
            variables,
            evaluationTime,
            eventState,
            deliveryState,
            result);
    }

    if (node.isObject()) {
        for (const auto& [fieldName, childNode] : node.asObject()) {
            processRulesRecursively(
                childNode,
                joinPath(pathText, fieldName),
                variables,
                evaluationTime,
                eventState,
                deliveryState,
                result);
        }
        return;
    }

    if (node.isArray()) {
        const auto& nodeArray = node.asArray();
        for (std::size_t indexValue = 0U; indexValue < nodeArray.size(); ++indexValue) {
            processRulesRecursively(
                nodeArray[indexValue],
                joinPath(pathText, std::to_string(indexValue)),
                variables,
                evaluationTime,
                eventState,
                deliveryState,
                result);
        }
    }
}

} // namespace

bool RuleRuntimeEngine::isRuleNodeStructureValid(const RuleTreeNode& ruleNode) {
    if (!ruleNode.isObject()) {
        return false;
    }

    const auto& ruleObject = ruleNode.asObject();
    const auto topicIter = ruleObject.find("topic");
    if (topicIter == ruleObject.end()) {
        return false;
    }

    return topicShapeValid(topicIter->second);
}

void RuleRuntimeEngine::ingestDomainMessageEvent(
    const Message& message,
    const std::chrono::system_clock::time_point& messageTime,
    const std::vector<std::string>& motionTopicFilters,
    RuleRuntimeEventState* eventState) {
    if (isZeroPayloadValue(message.value())) {
        return;
    }

    if (isMotionTopic(message.topic(), motionTopicFilters)) {
        eventState->motionEvents.push_back(MotionEventRecord{
            .topicName = message.topic(),
            .timestamp = messageTime,
            .sequenceNumber = eventState->nextSequenceNumber,
        });
        eventState->nextSequenceNumber += 1U;

        if (eventState->motionEvents.size() > k_max_motion_events) {
            const std::size_t eraseCount = std::min(k_motion_trim_count, eventState->motionEvents.size());
            eventState->motionEvents.erase(
                eventState->motionEvents.begin(),
                eventState->motionEvents.begin() + static_cast<std::ptrdiff_t>(eraseCount));
        }
        return;
    }

    eventState->nonMotionEvents.insert(message.topic());
}

RuleRuntimeProcessingResult RuleRuntimeEngine::processRules(
    const RuleTreeNode& rulesRoot,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeEventState* eventState,
    RuleRuntimeDeliveryState* deliveryState) {
    RuleRuntimeProcessingResult result{};
    processRulesRecursively(
        rulesRoot,
        std::string{},
        variables,
        evaluationTime,
        eventState,
        deliveryState,
        &result);
    return result;
}

std::vector<Message> RuleRuntimeEngine::previewDeliveredMessages(
    const std::string& rulePath,
    const RuleTreeNode& ruleNode,
    const std::vector<Message>& candidateMessages,
    const std::chrono::system_clock::time_point& evaluationTime,
    const RuleRuntimeDeliveryState& deliveryState) {
    if (!ruleNode.isObject()) {
        return {};
    }

    RuleRuntimeDeliveryState deliveryStateCopy = deliveryState;
    return applyDeliveryControls(
        rulePath,
        ruleNode.asObject(),
        candidateMessages,
        evaluationTime,
        &deliveryStateCopy);
}

RuleRuntimeRulePreviewResult RuleRuntimeEngine::previewRule(
    const std::string& rulePath,
    const RuleTreeNode& ruleNode,
    const ExpressionEvaluator::VariableMap& variables,
    const std::chrono::system_clock::time_point& evaluationTime,
    const RuleRuntimeEventState& eventState,
    const RuleRuntimeDeliveryState& deliveryState) {
    RuleRuntimeRulePreviewResult preview{};

    RuleRuntimeEventState eventStateCopy = eventState;
    RuleRuntimeDeliveryState deliveryStateCopy = deliveryState;
    RuntimeRuleEvaluationOutcome outcome = evaluateRuleNodeRuntime(
        ruleNode,
        rulePath,
        variables,
        evaluationTime,
        &eventStateCopy,
        &deliveryStateCopy,
        &preview.traceEntries);

    preview.success = outcome.success;
    preview.triggered = outcome.triggered;
    preview.candidateMessages = std::move(outcome.candidateMessages);
    preview.deliveredMessages = std::move(outcome.emittedMessages);
    preview.usedVariables = std::move(outcome.usedVariables);
    preview.errors = std::move(outcome.errors);
    return preview;
}

void RuleRuntimeEngine::clearNonMotionEvents(RuleRuntimeEventState* eventState) {
    eventState->nonMotionEvents.clear();
}

} // namespace yaha
