#include "yaha/message/message_log_formatter.h"

#include "yaha/message/message_payload_codec.h"

#include <string>

namespace yaha {

namespace {

[[nodiscard]] std::string quoteLogString(const std::string_view valueText) {
    return "\"" + escapeJsonString(valueText) + "\"";
}

[[nodiscard]] std::string boolToLogText(const bool flagValue) {
    return flagValue ? "true" : "false";
}

[[nodiscard]] std::string directionToLogText(const MessageLogDirection direction) {
    return direction == MessageLogDirection::Incoming ? "incoming" : "outgoing";
}

[[nodiscard]] std::string valueToLogToken(const Value& valueVariant) {
    if (std::holds_alternative<std::string>(valueVariant)) {
        return quoteLogString(std::get<std::string>(valueVariant));
    }

    return std::to_string(std::get<double>(valueVariant));
}

[[nodiscard]] std::string reasonChainToLogToken(const ReasonList& reasonEntries,
                                                const bool includeReasonChain) {
    if (!includeReasonChain) {
        return "[]";
    }

    std::string token{"["};
    for (std::size_t reasonIndex = 0U; reasonIndex < reasonEntries.size(); ++reasonIndex) {
        if (reasonIndex > 0U) {
            token.push_back(',');
        }

        const ReasonEntry& reasonEntry = reasonEntries[reasonIndex];
        token += "{\"message\":" + quoteLogString(reasonEntry.message)
            + ",\"timestamp\":" + quoteLogString(reasonEntry.timestamp) + "}";
    }
    token.push_back(']');
    return token;
}

} // namespace

std::string formatMessageLogLine(const std::string_view componentName,
                                 const MessageLogDirection direction,
                                 const Message& message,
                                 const bool includeReasonChain) {
    std::string line = "component=" + quoteLogString(componentName)
        + " direction=" + quoteLogString(directionToLogText(direction))
        + " topic=" + quoteLogString(message.topic())
        + " value=" + valueToLogToken(message.value())
        + " qos=" + std::to_string(static_cast<unsigned int>(message.qos()))
        + " retain=" + boolToLogText(message.retain())
        + " dup=" + boolToLogText(message.dup())
        + " reason=" + reasonChainToLogToken(message.reason(), includeReasonChain);

    if (message.rawPayload().has_value()) {
        line += " raw=" + quoteLogString(*message.rawPayload());
    }

    return line;
}

} // namespace yaha
