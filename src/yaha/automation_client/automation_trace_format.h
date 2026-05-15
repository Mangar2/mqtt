#pragma once

/**
 * @file automation_trace_format.h
 * @brief Helper functions for automation debug-trace formatting.
 */

#include <string>
#include <vector>

#include "yaha/message/message.h"

namespace yaha::automation_trace_format {

void appendTraceEntry(std::vector<std::string>* traceEntries, const std::string& traceText);
void appendExplainTraceEntries(
    std::vector<std::string>* traceEntries,
    const std::vector<std::string>& evaluationTrace,
    const std::string& fallbackTopic);
[[nodiscard]] std::string buildTraceRawPayload(const Message& traceMessage);

} // namespace yaha::automation_trace_format
