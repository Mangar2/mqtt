#include "yaha/automation_client/automation_trace_format.h"

#include <array>
#include <string>

namespace yaha::automation_trace_format {
namespace {

constexpr unsigned char k_ascii_control_max{0x20U};
constexpr unsigned char k_low_nibble_mask{0x0FU};

[[nodiscard]] std::string buildDebugExplainSummary(
    const std::string& topic,
    const std::string& checkReason,
    const std::string& valueReason) {
    if (topic.empty()) {
        return {};
    }

    std::string summary = "Rule: " + topic;
    if (!checkReason.empty()) {
        summary += ", check: " + checkReason;
    }
    if (!valueReason.empty()) {
        summary += ", value: " + valueReason;
    }
    return summary;
}

[[nodiscard]] std::string jsonEscapeString(const std::string& text) {
    std::string result{"\""};
    constexpr std::array<char, 16U> k_hex_digits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (const unsigned char currentChar : text) {
        switch (currentChar) {
        case '\\':
            result += "\\\\";
            break;
        case '\"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        default:
            if (currentChar < k_ascii_control_max) {
                result += "\\u00";
                result.push_back(k_hex_digits[(currentChar >> 4U) & k_low_nibble_mask]);
                result.push_back(k_hex_digits[currentChar & k_low_nibble_mask]);
            } else {
                result.push_back(static_cast<char>(currentChar));
            }
            break;
        }
    }
    result.push_back('\"');
    return result;
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
    const std::string& fallbackTopic) {
    constexpr std::string_view k_topic_prefix{"rule-evaluation:topic="};
    constexpr std::string_view k_check_reason_prefix{"rule-evaluation:check reason="};
    constexpr std::string_view k_value_reason_prefix{"rule-evaluation:value reason="};
    constexpr std::string_view k_error_prefix{"rule-evaluation:error "};

    std::string topic = fallbackTopic;
    std::string checkReason;
    std::string valueReason;

    for (const auto& entry : evaluationTrace) {
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

    const std::string summary = buildDebugExplainSummary(topic, checkReason, valueReason);
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
    std::string reasonArray{"["};
    const auto& reasonEntries = traceMessage.reason();
    for (std::size_t index = 0U; index < reasonEntries.size(); ++index) {
        if (index > 0U) {
            reasonArray.push_back(',');
        }
        reasonArray += "{\"message\":" + jsonEscapeString(reasonEntries[index].message)
            + ",\"timestamp\":" + jsonEscapeString(reasonEntries[index].timestamp) + "}";
    }
    reasonArray.push_back(']');

    const std::string valueText = std::holds_alternative<std::string>(traceMessage.value())
        ? jsonEscapeString(std::get<std::string>(traceMessage.value()))
        : std::to_string(std::get<double>(traceMessage.value()));

    return std::string{R"({"message":{"topic":)"} + jsonEscapeString(traceMessage.topic())
        + R"(,"value":)" + valueText
        + R"(,"reason":)" + reasonArray + "}}";
}

} // namespace yaha::automation_trace_format
