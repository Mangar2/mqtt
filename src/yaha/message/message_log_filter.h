#pragma once

#include <string_view>

namespace yaha {

[[nodiscard]] bool matchesTopicFilter(std::string_view topicName, std::string_view topicFilter);

} // namespace yaha
