#include "yaha/automation_client/automation_trace_format.h"
#include "yaha/message/message_payload_codec.h"

#include <optional>
#include <string>

namespace yaha::automation_trace_format {
namespace {

[[nodiscard]] std::string toPassedFailedText(const bool passed) {
    return passed ? "passed" : "failed";
}

[[nodiscard]] std::string simplifyEvaluationValueText(const std::string& valueText) {
    const std::size_t separatorIndex = valueText.find(':');
    if (separatorIndex == std::string::npos || separatorIndex + 1U >= valueText.size()) {
        return valueText;
    }
    return valueText.substr(separatorIndex + 1U);
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
    constexpr std::string_view k_check_decision_prefix{"rule-evaluation:check decision="};
    constexpr std::string_view k_check_reason_prefix{"rule-evaluation:check reason="};
    constexpr std::string_view k_value_result_prefix{"rule-evaluation:value result="};
    constexpr std::string_view k_value_reason_prefix{"rule-evaluation:value reason="};
    constexpr std::string_view k_error_prefix{"rule-evaluation:error "};

    std::optional<bool> checkPassed;
    std::string checkReason{};
    std::string valueReason{};
    std::string valueResult{};
    (void)fallbackRuleIdentifier;

    for (const auto& entry : evaluationTrace) {
        if (!checkPassed.has_value() && entry.starts_with(k_check_decision_prefix)) {
            const std::string decisionText = entry.substr(k_check_decision_prefix.size());
            if (decisionText == "trigger") {
                checkPassed = true;
            } else if (decisionText == "skip") {
                checkPassed = false;
            }
            continue;
        }
        if (checkReason.empty() && entry.starts_with(k_check_reason_prefix)) {
            checkReason = entry.substr(k_check_reason_prefix.size());
            continue;
        }
        if (valueResult.empty() && entry.starts_with(k_value_result_prefix)) {
            valueResult = simplifyEvaluationValueText(entry.substr(k_value_result_prefix.size()));
            continue;
        }
        if (valueReason.empty() && entry.starts_with(k_value_reason_prefix)) {
            valueReason = entry.substr(k_value_reason_prefix.size());
            continue;
        }
        if (entry.starts_with(k_error_prefix)) {
            appendTraceEntry(traceEntries, "error: " + entry.substr(k_error_prefix.size()));
        }
    }

    if (checkPassed.has_value()) {
        if (checkReason.empty()) {
            appendTraceEntry(
                traceEntries,
                "check: " + toPassedFailedText(*checkPassed));
        } else {
            appendTraceEntry(
                traceEntries,
                "check: " + toPassedFailedText(*checkPassed)
                    + " (" + checkReason + ")");
        }
    }

    if (!valueReason.empty()) {
        appendTraceEntry(traceEntries, "value: " + valueReason + " (evaluation result)");
    } else if (!valueResult.empty()) {
        appendTraceEntry(traceEntries, "value: " + valueResult + " (evaluation result)");
    }
}

std::string buildTraceRawPayload(const Message& traceMessage) {
    return buildEnvelopePayload(traceMessage);
}

} // namespace yaha::automation_trace_format
