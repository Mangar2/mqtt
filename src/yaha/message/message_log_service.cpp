#include "yaha/message/message_log_service.h"

#include "yaha/message/message_log_filter.h"

namespace yaha {

namespace {

[[nodiscard]] bool matchesConfiguredFilter(const std::optional<std::string>& configuredFilter,
                                           const Message& message) {
    if (!configuredFilter.has_value() || configuredFilter->empty()) {
        return true;
    }

    return matchesTopicFilter(message.topic(), *configuredFilter);
}

} // namespace

bool shouldLogMessage(const MessageLogDirection direction,
                      const Message& message,
                      const MessageLogConfig& config) {
    if (direction == MessageLogDirection::Incoming) {
        return config.enableIncoming && matchesConfiguredFilter(config.incomingTopicFilter, message);
    }

    return config.enableOutgoing && matchesConfiguredFilter(config.outgoingTopicFilter, message);
}

std::optional<std::string> buildMessageLogLine(const std::string_view componentName,
                                               const MessageLogDirection direction,
                                               const Message& message,
                                               const MessageLogConfig& config) {
    if (!shouldLogMessage(direction, message, config)) {
        return std::nullopt;
    }

    return formatMessageLogLine(componentName, direction, message, config.includeReasonChain);
}

} // namespace yaha
