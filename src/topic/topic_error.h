#pragma once

/**
 * @file topic_error.h
 * @brief Error codes and exception type for the Topic Engine (Module 3).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes produced by topic validation.
 */
enum class TopicError : uint8_t {
  EmptyTopic,    ///< Zero-length topic name or filter.
  TopicTooLong,  ///< Byte length exceeds 65 535 (MQTT UTF-8 string limit).
  NullCharacter, ///< U+0000 present — forbidden by MQTT Section 1.5.4.
  WildcardInTopicName, ///< '+' or '#' used in a publish topic name.
  InvalidWildcard, ///< Wildcard character appears in an illegal position in a
                   ///< filter.
};

/**
 * @brief Exception thrown by topic validation functions.
 *
 * Carries the specific @ref TopicError that caused the failure so callers
 * can map it to an appropriate MQTT reason code.
 */
class TopicException : public std::runtime_error {
public:
  /**
   * @brief Constructs a TopicException with the given error code.
   * @param err The validation error that was detected.
   */
  explicit TopicException(TopicError err)
      : std::runtime_error{make_message(err)}, error_{err} {}

  /**
   * @brief Returns the error code that caused this exception.
   * @return The TopicError value.
   */
  [[nodiscard]] TopicError error() const noexcept { return error_; }

private:
  TopicError error_; ///< Error code for this exception.

  static std::string make_message(TopicError err) {
    switch (err) {
    case TopicError::EmptyTopic:
      return "Topic name or filter must not be empty";
    case TopicError::TopicTooLong:
      return "Topic name or filter exceeds 65535 bytes";
    case TopicError::NullCharacter:
      return "Topic name or filter contains a null character (U+0000)";
    case TopicError::WildcardInTopicName:
      return "Wildcard character in publish topic name";
    case TopicError::InvalidWildcard:
      return "Wildcard character in invalid position in topic filter";
    }
    return "Unknown topic error";
  }
};

} // namespace mqtt
