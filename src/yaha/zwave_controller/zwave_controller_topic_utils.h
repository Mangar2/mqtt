#pragma once

#include "yaha/message/message.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaha::zwave_controller_topic_utils {

inline constexpr std::size_t kSetTopicMinimumParts = 2U;

[[nodiscard]] std::optional<std::uint16_t> parseNodeIdFromValue(const Value& value);
[[nodiscard]] std::optional<std::string> parseOptionalLabelFromSetTopic(const std::vector<std::string>& topicParts);
[[nodiscard]] std::string joinTopicParts(const std::vector<std::string>& parts, std::size_t count);
[[nodiscard]] std::vector<std::string> splitTopic(const std::string& topic);

} // namespace yaha::zwave_controller_topic_utils
