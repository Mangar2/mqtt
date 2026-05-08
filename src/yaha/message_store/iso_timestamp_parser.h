#pragma once

/**
 * @file iso_timestamp_parser.h
 * @brief ISO-8601 timestamp parsing helpers for MessageStore internals.
 */

#include <cstdint>
#include <string>

namespace yaha {

/**
 * @brief Parses one ISO-8601 timestamp into Unix epoch milliseconds.
 * @param timestampText Timestamp in ISO-8601 format with timezone information.
 * @param outMilliseconds Parsed Unix epoch milliseconds when parsing succeeds.
 * @return True when parsing succeeded.
 */
[[nodiscard]] bool tryParseIsoTimestampMilliseconds(const std::string& timestampText,
                                                    std::int64_t& outMilliseconds);

/**
 * @brief Formats Unix epoch milliseconds as ISO-8601 UTC timestamp.
 * @param millisecondsSinceEpoch Unix epoch milliseconds.
 * @return ISO-8601 UTC timestamp with millisecond precision.
 */
[[nodiscard]] std::string toIsoTimestampMilliseconds(std::int64_t millisecondsSinceEpoch);

} // namespace yaha
