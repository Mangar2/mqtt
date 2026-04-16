#include "topic/topic_validator.h"

#include <cstddef>

#include "data_model/types/utf8_string.h"
#include "topic/topic_error.h"

namespace mqtt {

namespace {

constexpr char k_wildcard_hash = '#';
constexpr char k_wildcard_plus = '+';
constexpr char k_level_separator = '/';
constexpr char k_null_char = '\0';
constexpr char k_system_prefix = '$';

void check_common(std::string_view str) {
  if (str.empty()) {
    throw TopicException{TopicError::EmptyTopic};
  }
  if (str.size() > Utf8String::k_max_byte_length) {
    throw TopicException{TopicError::TopicTooLong};
  }
  if (str.find(k_null_char) != std::string_view::npos) {
    throw TopicException{TopicError::NullCharacter};
  }
}

} // anonymous namespace

void validate_topic_name(std::string_view topic) {
  check_common(topic);

  for (const char chr : topic) {
    if (chr == k_wildcard_plus || chr == k_wildcard_hash) {
      throw TopicException{TopicError::WildcardInTopicName};
    }
  }
}

void validate_topic_filter(std::string_view filter) {
  check_common(filter);

  const std::size_t len = filter.size();

  for (std::size_t idx = 0; idx < len; ++idx) {
    const char chr = filter[idx];

    if (chr == k_wildcard_hash) {
      // '#' must be the last character and must be preceded by '/' or be at
      // position 0.
      const bool at_end = (idx == len - 1U);
      const bool preceded_by_sep =
          (idx == 0U) || (filter[idx - 1U] == k_level_separator);
      if (!at_end || !preceded_by_sep) {
        throw TopicException{TopicError::InvalidWildcard};
      }
    } else if (chr == k_wildcard_plus) {
      // '+' must occupy an entire level: must be preceded by '/' or
      // start-of-string and followed by '/' or end-of-string.
      const bool preceded_ok =
          (idx == 0U) || (filter[idx - 1U] == k_level_separator);
      const bool followed_ok =
          (idx == len - 1U) || (filter[idx + 1U] == k_level_separator);
      if (!preceded_ok || !followed_ok) {
        throw TopicException{TopicError::InvalidWildcard};
      }
    }
  }
}

bool is_system_topic(std::string_view topic) noexcept {
  return !topic.empty() && topic.front() == k_system_prefix;
}

} // namespace mqtt
