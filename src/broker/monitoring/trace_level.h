#pragma once

/**
 * @file trace_level.h
 * @brief Trace level definitions and conversion helpers for structured tracing
 *        (Module 26).
 */

#include <cstdint>
#include <optional>
#include <string_view>

namespace mqtt {

/**
 * @brief Hierarchical trace severity used by structured tracing.
 *
 * Ordering is `none < error < warning < info < trace`.
 */
enum class TraceLevel : std::uint8_t {
  None = 0U,    ///< Disable all tracing output.
  Error = 1U,   ///< Program faults and impossible internal states.
  Warning = 2U, ///< Degraded or invalid client-driven states.
  Info = 3U,    ///< MQTT lifecycle events (CONNECT, SUBSCRIBE, DISCONNECT).
  Trace = 4U,   ///< Detailed diagnostics for debugging.
};

/**
 * @brief Convert a trace level to its lower-case textual representation.
 *
 * @param level Trace level value.
 * @return One of: "none", "error", "warning", "info", "trace".
 */
[[nodiscard]] std::string_view to_string(TraceLevel level) noexcept;

/**
 * @brief Parse a textual trace level (case-insensitive).
 *
 * @param level_name Input level text.
 * @return Parsed level when recognised; otherwise `std::nullopt`.
 */
[[nodiscard]] std::optional<TraceLevel> parse_trace_level(std::string_view level_name) noexcept;

} // namespace mqtt
