#pragma once

#include "yaha/message/message.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace yaha {

enum class MessageLogDirection : std::uint8_t {
    Incoming = 0U,
    Outgoing = 1U
};

[[nodiscard]] std::string formatMessageLogLine(
    std::string_view componentName,
    MessageLogDirection direction,
    const Message& message,
    bool includeReasonChain = true);

} // namespace yaha
