#pragma once

#include "yaha/ini/ini_document.h"
#include "yaha/message/message.h"
#include "yaha/message/message_log_formatter.h"

#include <optional>
#include <string>
#include <string_view>

namespace yaha {

struct MessageLogConfig {
    bool enableIncoming{false};
    bool enableOutgoing{false};
    bool includeReasonChain{true};
    std::optional<std::string> incomingTopicFilter{};
    std::optional<std::string> outgoingTopicFilter{};
};

struct MessageLogIniBoolKey {
    std::string_view section{};
    std::string_view key{};
};

struct MessageLogIniKeys {
    std::optional<MessageLogIniBoolKey> incomingEnabled{};
    std::optional<MessageLogIniBoolKey> outgoingEnabled{};
    std::optional<MessageLogIniBoolKey> includeReasonChain{};
};

[[nodiscard]] bool tryLoadMessageLogConfigFromIni(
    const IniDocument& document,
    const MessageLogIniKeys& keys,
    MessageLogConfig& config,
    std::string& errorMessage);

[[nodiscard]] bool shouldLogMessage(
    MessageLogDirection direction,
    const Message& message,
    const MessageLogConfig& config);

[[nodiscard]] std::optional<std::string> buildMessageLogLine(
    std::string_view componentName,
    MessageLogDirection direction,
    const Message& message,
    const MessageLogConfig& config);

} // namespace yaha
