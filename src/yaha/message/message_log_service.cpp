#include "yaha/message/message_log_service.h"

#include "yaha/message/message_log_filter.h"

#include <string_view>

namespace yaha {

namespace {

[[nodiscard]] bool tryReadBoolWithKey(const IniDocument& document,
                                      const std::optional<MessageLogIniBoolKey>& configuredKey,
                                      std::optional<bool>& value,
                                      std::string& errorMessage) {
    if (!configuredKey.has_value()) {
        return true;
    }

    value = document.readBool(configuredKey->section, configuredKey->key, false);
    errorMessage.clear();
    return true;
}

[[nodiscard]] bool matchesConfiguredFilter(const std::optional<std::string>& configuredFilter,
                                           const Message& message) {
    if (!configuredFilter.has_value() || configuredFilter->empty()) {
        return true;
    }

    return matchesTopicFilter(message.topic(), *configuredFilter);
}

} // namespace

bool tryLoadMessageLogConfigFromIni(const IniDocument& document,
                                    const MessageLogIniKeys& keys,
                                    MessageLogConfig& config,
                                    std::string& errorMessage) {
    errorMessage.clear();

    std::optional<bool> parsedIncoming{};
    std::optional<bool> parsedOutgoing{};
    std::optional<bool> parsedReason{};

    if (!tryReadBoolWithKey(document, keys.incomingEnabled, parsedIncoming, errorMessage)) {
        return false;
    }
    if (!tryReadBoolWithKey(document, keys.outgoingEnabled, parsedOutgoing, errorMessage)) {
        return false;
    }
    if (!tryReadBoolWithKey(document, keys.includeReasonChain, parsedReason, errorMessage)) {
        return false;
    }

    if (parsedIncoming.has_value()) {
        config.enableIncoming = *parsedIncoming;
    }
    if (parsedOutgoing.has_value()) {
        config.enableOutgoing = *parsedOutgoing;
    }
    if (parsedReason.has_value()) {
        config.includeReasonChain = *parsedReason;
    }

    errorMessage.clear();
    return true;
}

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
