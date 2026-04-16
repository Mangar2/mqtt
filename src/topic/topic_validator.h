#pragma once

/**
 * @file topic_validator.h
 * @brief MQTT 5.0 topic-name and topic-filter validation (Module 3.1).
 */

#include <string_view>

namespace mqtt {

/**
 * @brief Validates an MQTT publish topic name (Section 4.7.1 / 4.7.3).
 *
 * A valid topic name:
 * - Is between 1 and 65 535 bytes long.
 * - Contains no null character (U+0000).
 * - Contains no wildcard characters ('+' or '#').
 *
 * @param topic The topic name to validate.
 * @throws TopicException(TopicError::EmptyTopic)          if @p topic is empty.
 * @throws TopicException(TopicError::TopicTooLong)        if @p topic exceeds
 * 65 535 bytes.
 * @throws TopicException(TopicError::NullCharacter)       if @p topic contains
 * U+0000.
 * @throws TopicException(TopicError::WildcardInTopicName) if @p topic contains
 * '+' or '#'.
 */
void validate_topic_name(std::string_view topic);

/**
 * @brief Validates an MQTT subscription topic filter (Section 4.7.1).
 *
 * A valid topic filter:
 * - Is between 1 and 65 535 bytes long.
 * - Contains no null character (U+0000).
 * - Uses '#' only as the last character, either alone or immediately after '/'.
 * - Uses '+' only to occupy an entire topic level (surrounded by '/' or at
 * boundaries).
 *
 * @param filter The topic filter to validate.
 * @throws TopicException(TopicError::EmptyTopic)     if @p filter is empty.
 * @throws TopicException(TopicError::TopicTooLong)   if @p filter exceeds 65
 * 535 bytes.
 * @throws TopicException(TopicError::NullCharacter)  if @p filter contains
 * U+0000.
 * @throws TopicException(TopicError::InvalidWildcard) if a wildcard appears in
 * an illegal position.
 */
void validate_topic_filter(std::string_view filter);

/**
 * @brief Detects whether a topic is a system topic (Section 4.7.2).
 *
 * A topic is a system topic when its first byte is '$'.
 * Returns false for an empty string.
 *
 * @param topic The topic name or filter to inspect.
 * @return true if @p topic starts with '$', false otherwise.
 */
[[nodiscard]] bool is_system_topic(std::string_view topic) noexcept;

} // namespace mqtt
