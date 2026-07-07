#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation_client_rule_runtime/rule_runtime_engine.h"
#include "yaha/message/message.h"

namespace yaha {

[[nodiscard]] std::string valueToStableText(const Value& messageValue);
[[nodiscard]] bool isZeroPayloadValue(const Value& messageValue);
[[nodiscard]] std::optional<double> readPositiveGateSeconds(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName);
[[nodiscard]] std::vector<Message> applyDeliveryControls(
    const std::string& rulePath,
    const RuleTreeNode::Object& ruleObject,
    const std::vector<Message>& candidateMessages,
    const std::chrono::system_clock::time_point& evaluationTime,
    RuleRuntimeDeliveryState* deliveryState);
[[nodiscard]] bool shouldRetainDeliveryStateOnGateMiss(const RuleTreeNode::Object& ruleObject);
void clearRuleDeliveryState(const std::string& rulePath, RuleRuntimeDeliveryState* deliveryState);

} // namespace yaha
