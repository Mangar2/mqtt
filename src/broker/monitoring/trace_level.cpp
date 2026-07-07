/**
 * @file trace_level.cpp
 * @brief Trace level conversion helpers (Module 26).
 */

#include "monitoring/trace_level.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace mqtt {

std::string_view to_string(TraceLevel level) noexcept {
  switch (level) {
  case TraceLevel::None:
    return "none";
  case TraceLevel::Error:
    return "error";
  case TraceLevel::Warning:
    return "warning";
  case TraceLevel::Info:
    return "info";
  case TraceLevel::Trace:
    return "trace";
  }

  return "none";
}

std::optional<TraceLevel> parse_trace_level(std::string_view level_name) noexcept {
  std::string lower(level_name);
  std::ranges::transform(lower, lower.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });

  if (lower == "none") {
    return TraceLevel::None;
  }
  if (lower == "error") {
    return TraceLevel::Error;
  }
  if (lower == "warning") {
    return TraceLevel::Warning;
  }
  if (lower == "info") {
    return TraceLevel::Info;
  }
  if (lower == "trace") {
    return TraceLevel::Trace;
  }

  return std::nullopt;
}

} // namespace mqtt
