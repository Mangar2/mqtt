#pragma once

/**
 * @file structured_tracer.h
 * @brief Structured tracing in JSON Lines format (Module 26).
 */

#include <chrono>
#include <atomic>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "monitoring/trace_level.h"

namespace mqtt {

#ifndef MQTT_TRACING_DISABLED
#define TRACE_GUARD(tracer, level, module)                                       \
  if ((tracer) != nullptr && (tracer)->should_emit((level), (module)))
#else
#define TRACE_GUARD(tracer, level, module) if constexpr (false)
#endif

/**
 * @brief Structured trace payload represented as key-value pairs.
 */
using TraceDataFields = std::vector<std::pair<std::string, std::string>>;

/**
 * @brief One structured trace event to emit as one JSON line.
 */
struct TraceEvent {
  TraceLevel level{TraceLevel::Info}; ///< Event severity.
  std::string module;                 ///< Stable module identifier.
  std::string info;                   ///< Short event summary.
  std::optional<std::string> detail;  ///< Optional secondary description.
  TraceDataFields data;               ///< Optional diagnostic payload fields.
  std::chrono::system_clock::time_point
      timestamp{std::chrono::system_clock::now()}; ///< Event time.
};

/**
 * @brief JSON-lines structured tracer with hierarchical filtering.
 *
 * Filtering model:
 * - Global level controls `error`, `warning`, and `info`.
 * - `trace` events are emitted when global level is `trace` OR the event
 *   module is explicitly enabled for trace.
 * - `none` disables all output.
 *
 * Each `emit()` writes exactly one JSON object line.
 */
class StructuredTracer {
public:
  /**
   * @brief Construct a tracer writing to @p output_stream.
   * @param output_stream Output sink for JSON lines.
   */
  explicit StructuredTracer(std::ostream &output_stream);

  /**
   * @brief Replace the current output stream sink.
   * @param output_stream New output sink.
   */
  void set_output(std::ostream &output_stream);

  /**
   * @brief Set the global trace threshold.
   * @param level New global level.
   */
  void set_global_level(TraceLevel level) noexcept;

  /**
   * @brief Return the current global trace threshold.
   * @return Current global level.
   */
  [[nodiscard]] TraceLevel global_level() const noexcept;

  /**
   * @brief Enable trace-level output for one module.
   * @param module_name Stable module identifier.
   */
  void enable_trace_module(std::string module_name);

  /**
   * @brief Disable trace-level output for one module.
   * @param module_name Stable module identifier.
   */
  void disable_trace_module(std::string_view module_name);

  /**
   * @brief Replace the current module trace override set.
   * @param module_names Modules for which trace is enabled.
   */
  void set_trace_modules(const std::vector<std::string> &module_names);

  /**
   * @brief Set maximum length for each serialized text field.
   * @param max_text_length Maximum number of bytes per text field.
   */
  void set_max_text_length(std::size_t max_text_length) noexcept;

  /**
   * @brief Return configured maximum length for serialized text fields.
   * @return Maximum number of bytes per text field.
   */
  [[nodiscard]] std::size_t max_text_length() const noexcept;

  /**
   * @brief Return whether the event would pass current filtering.
   *
   * @param level Event level.
   * @param module_name Event module.
   * @return `true` when an event with this level and module would be emitted.
   */
  [[nodiscard]] bool should_emit(TraceLevel level,
                                 std::string_view module_name) const noexcept;

  /**
   * @brief Emit one structured trace event as one JSON object line.
   *
   * If JSON serialisation fails, emits a minimal fallback error record instead.
   *
   * @param event Event to emit.
   */
  void emit(const TraceEvent &event);

private:
  [[nodiscard]] bool should_emit_unlocked(TraceLevel level,
                                          std::string_view module_name) const noexcept;

  static void write_json_string(std::ostream &output_stream,
                                std::string_view value_text);

  /**
   * @brief Write fallback error record for serialization failures.
   * @param output_stream Target output stream.
   */
  static void write_fallback_serialization_error(std::ostream &output_stream) noexcept;

  std::ostream *output_stream_;
  std::atomic<TraceLevel> global_level_{TraceLevel::Warning};
  std::atomic<std::size_t> max_text_length_{2024U};
  std::unordered_set<std::string> trace_modules_;
  mutable std::mutex mutex_;
};

} // namespace mqtt
