#include "yaha/automation_client/automation_trace_format.h"
#include "yaha/message/message_payload_codec.h"

#include <string>

namespace yaha::automation_trace_format {
namespace {

[[nodiscard]] std::string buildDebugExplainSummary(
    const std::string& ruleIdentifier,
    const std::string& checkReason,
    const std::string& valueReason) {
    if (ruleIdentifier.empty()) {
        return {};
    }

    std::string summary = "Rule: " + ruleIdentifier;
    if (!checkReason.empty()) {
        summary += ", check: " + checkReason;
    }
    if (!valueReason.empty()) {
        summary += ", value: " + valueReason;
    }
    return summary;
}

} // namespace

void appendTraceEntry(std::vector<std::string>* traceEntries, const std::string& traceText) {
    if (traceEntries == nullptr) {
        return;
    }
    traceEntries->push_back(traceText);
}

void appendExplainTraceEntries(
    std::vector<std::string>* traceEntries,
    const std::vector<std::string>& evaluationTrace,
    const std::string& fallbackRuleIdentifier) {
    constexpr std::string_view k_rule_prefix{"rule-evaluation:rule="};
    constexpr std::string_view k_topic_prefix{"rule-evaluation:topic="};
    constexpr std::string_view k_check_reason_prefix{"rule-evaluation:check reason="};
    constexpr std::string_view k_value_reason_prefix{"rule-evaluation:value reason="};
    constexpr std::string_view k_error_prefix{"rule-evaluation:error "};

    std::string ruleIdentifier = fallbackRuleIdentifier;
    std::string topic;
    std::string checkReason;
    std::string valueReason;

    for (const auto& entry : evaluationTrace) {
        if (ruleIdentifier.empty() && entry.starts_with(k_rule_prefix)) {
            ruleIdentifier = entry.substr(k_rule_prefix.size());
            continue;
        }
        if (topic.empty() && entry.starts_with(k_topic_prefix)) {
            topic = entry.substr(k_topic_prefix.size());
            continue;
        }
        if (checkReason.empty() && entry.starts_with(k_check_reason_prefix)) {
            checkReason = entry.substr(k_check_reason_prefix.size());
            continue;
        }
        if (valueReason.empty() && entry.starts_with(k_value_reason_prefix)) {
            valueReason = entry.substr(k_value_reason_prefix.size());
            continue;
        }
        if (entry.starts_with(k_error_prefix)) {
            appendTraceEntry(traceEntries, "debug:error " + entry.substr(k_error_prefix.size()));
        }
    }

    if (ruleIdentifier.empty()) {
        ruleIdentifier = topic;
    }

    const std::string summary = buildDebugExplainSummary(ruleIdentifier, checkReason, valueReason);
    if (!summary.empty()) {
        appendTraceEntry(traceEntries, "debug:explain " + summary);
        return;
    }

    if (!checkReason.empty()) {
        appendTraceEntry(traceEntries, "debug:explain check: " + checkReason);
    }
    if (!valueReason.empty()) {
        appendTraceEntry(traceEntries, "debug:explain value: " + valueReason);
    }
}

std::string buildTraceRawPayload(const Message& traceMessage) {
    return buildEnvelopePayload(traceMessage);
}

} // namespace yaha::automation_trace_format
