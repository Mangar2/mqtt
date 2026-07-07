#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation_client_rule_runtime/rule_runtime_engine.h"

namespace yaha {

void appendRuntimeTrace(std::vector<std::string>* traceEntries, const std::string& traceText);
void appendConfiguredEventGateTraceEntries(
    const RuleTreeNode::Object& ruleObject,
    const RuleRuntimeEventState& eventState,
    const std::chrono::system_clock::time_point& evaluationTime,
    std::vector<std::string>* traceEntries);

} // namespace yaha
