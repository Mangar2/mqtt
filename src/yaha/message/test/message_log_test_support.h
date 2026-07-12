#pragma once

#include "yaha/message/message_log_formatter.h"

#include <string>
#include <string_view>

namespace yaha::test {

[[nodiscard]] inline std::string messageLogArrow(const std::string_view componentName,
                                                  const MessageLogDirection direction) {
    return std::string{componentName} + (direction == MessageLogDirection::Incoming ? " <-" : " ->");
}

[[nodiscard]] inline std::string messageLogLinePrefix(const std::string_view componentName,
                                                       const MessageLogDirection direction,
                                                       const std::string_view topic) {
    return messageLogArrow(componentName, direction) + " " + std::string{topic} + " :";
}

} // namespace yaha::test
