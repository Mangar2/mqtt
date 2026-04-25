/**
 * @file structured_tracer.cpp
 * @brief Structured tracer implementation (Module 26).
 */

#include "monitoring/structured_tracer.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mqtt {

namespace {

[[nodiscard]] int to_severity_rank(TraceLevel level) noexcept {
  return static_cast<int>(level);
}

[[nodiscard]] std::string to_iso8601_utc(
    std::chrono::system_clock::time_point time_point) {
  const std::time_t timestamp_seconds =
      std::chrono::system_clock::to_time_t(time_point);

  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &timestamp_seconds);
#else
  gmtime_r(&timestamp_seconds, &utc_tm);
#endif

  const auto epoch_duration = time_point.time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(epoch_duration) %
      1000;

  std::ostringstream formatter;
  formatter << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S") << '.'
            << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
  return formatter.str();
}

[[nodiscard]] std::string truncate_text(std::string_view value_text,
                                        std::size_t max_text_length) {
  if (value_text.size() <= max_text_length) {
    return std::string(value_text);
  }
  if (max_text_length == 0U) {
    return {};
  }

  static constexpr std::string_view k_suffix = "...<truncated>";
  if (max_text_length <= k_suffix.size()) {
    return std::string(value_text.substr(0U, max_text_length));
  }

  std::string truncated(value_text.substr(0U, max_text_length - k_suffix.size()));
  truncated.append(k_suffix);
  return truncated;
}

void update_trace_theme_window(auto &stats,
                               std::chrono::system_clock::time_point now) {
  ++stats.total_count;

  if (!stats.has_t1) {
    stats.has_t1 = true;
    stats.t1 = now;
    stats.count_t1 = stats.total_count;
    return;
  }

  if (!stats.has_t2) {
    if ((now - stats.t1) >= std::chrono::seconds(1)) {
      stats.has_t2 = true;
      stats.t2 = now;
      stats.count_t2 = stats.total_count;
    }
    return;
  }

  if ((now - stats.t2) >= std::chrono::seconds(1)) {
    stats.t1 = stats.t2;
    stats.count_t1 = stats.count_t2;
    stats.t2 = now;
    stats.count_t2 = stats.total_count;
  }
}

[[nodiscard]] double traces_per_second(const auto &stats) {
  if (!stats.has_t2) {
    return 0.0;
  }

  const std::chrono::duration<double> period = stats.t2 - stats.t1;
  if (period.count() <= 0.0) {
    return 0.0;
  }

  const std::uint64_t delta_count = stats.count_t2 - stats.count_t1;
  return static_cast<double>(delta_count) / period.count();
}

} // namespace

StructuredTracer::StructuredTracer(std::ostream &output_stream)
    : output_stream_(&output_stream) {}

void StructuredTracer::set_output(std::ostream &output_stream) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  output_stream_ = &output_stream;
}

void StructuredTracer::set_global_level(TraceLevel level) noexcept {
  global_level_.store(level, std::memory_order_relaxed);
}

TraceLevel StructuredTracer::global_level() const noexcept {
  return global_level_.load(std::memory_order_relaxed);
}

void StructuredTracer::enable_trace_module(std::string module_name) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  if (!module_name.empty()) {
    trace_modules_.insert(std::move(module_name));
  }
}

void StructuredTracer::disable_trace_module(std::string_view module_name) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  trace_modules_.erase(std::string(module_name));
}

void StructuredTracer::set_trace_modules(const std::vector<std::string> &module_names) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  trace_modules_.clear();
  for (const std::string &module_name : module_names) {
    if (!module_name.empty()) {
      trace_modules_.insert(module_name);
    }
  }
}

void StructuredTracer::set_max_text_length(std::size_t max_text_length) noexcept {
  max_text_length_.store(max_text_length, std::memory_order_relaxed);
}

std::size_t StructuredTracer::max_text_length() const noexcept {
  return max_text_length_.load(std::memory_order_relaxed);
}

bool StructuredTracer::should_emit(TraceLevel level,
                                   std::string_view module_name) const noexcept {
  return should_emit_unlocked(level, module_name);
}

void StructuredTracer::emit(const TraceEvent &event) {
  if (!should_emit_unlocked(event.level, event.module)) {
    return;
  }

  std::lock_guard<std::mutex> lock_guard(mutex_);
  if (output_stream_ == nullptr) {
    return;
  }

  try {
    std::ostream &output_stream = *output_stream_;
    const std::size_t max_text_length =
        max_text_length_.load(std::memory_order_relaxed);
    TraceThemeStats &theme_stats = trace_theme_stats_[event.info];
    update_trace_theme_window(theme_stats, event.timestamp);
    const std::string module_text =
        truncate_text(event.module, max_text_length);
    const std::string info_text = truncate_text(event.info, max_text_length);
    output_stream << '{';

    output_stream << "\"timestamp\":";
    write_json_string(output_stream, to_iso8601_utc(event.timestamp));

    output_stream << ",\"level\":";
    write_json_string(output_stream, to_string(event.level));

    output_stream << ",\"module\":";
    write_json_string(output_stream, module_text);

    output_stream << ",\"info\":";
    write_json_string(output_stream, info_text);

    output_stream << ",\"theme_count\":" << theme_stats.total_count;

    const std::ios::fmtflags previous_flags = output_stream.flags();
    const std::streamsize previous_precision = output_stream.precision();
    output_stream << ",\"theme_rate_per_second\":" << std::fixed
                  << std::setprecision(3) << traces_per_second(theme_stats);
    output_stream.flags(previous_flags);
    output_stream.precision(previous_precision);

    if (event.detail.has_value()) {
      output_stream << ",\"detail\":";
      write_json_string(output_stream,
                        truncate_text(*event.detail, max_text_length));
    }

    if (!event.data.empty()) {
      output_stream << ",\"data\":{";
      bool is_first_field = true;
      for (const auto &[field_name, field_value] : event.data) {
        if (!is_first_field) {
          output_stream << ',';
        }
        write_json_string(output_stream,
                          truncate_text(field_name, max_text_length));
        output_stream << ':';
        write_json_string(output_stream,
                          truncate_text(field_value, max_text_length));
        is_first_field = false;
      }
      output_stream << '}';
    }

    output_stream << "}\n";
    if (!output_stream.good()) {
      throw std::runtime_error("structured trace write failed");
    }
  } catch (...) {
    if (output_stream_ != nullptr) {
      output_stream_->clear();
      write_fallback_serialization_error(*output_stream_);
    }
  }
}

bool StructuredTracer::should_emit_unlocked(TraceLevel level,
                                            std::string_view module_name) const noexcept {
  const TraceLevel global_level = global_level_.load(std::memory_order_relaxed);

  if (global_level == TraceLevel::None || level == TraceLevel::None) {
    return false;
  }

  if (level == TraceLevel::Trace) {
    if (global_level == TraceLevel::Trace) {
      return true;
    }

    std::lock_guard<std::mutex> lock_guard(mutex_);
    return trace_modules_.contains(std::string(module_name));
  }

  return to_severity_rank(level) <= to_severity_rank(global_level);
}

void StructuredTracer::write_json_string(std::ostream &output_stream,
                                         std::string_view value_text) {
  output_stream << '"';
  for (const char character : value_text) {
    switch (character) {
    case '\\':
      output_stream << "\\\\";
      break;
    case '"':
      output_stream << "\\\"";
      break;
    case '\n':
      output_stream << "\\n";
      break;
    case '\r':
      output_stream << "\\r";
      break;
    case '\t':
      output_stream << "\\t";
      break;
    default:
      output_stream << character;
      break;
    }
  }
  output_stream << '"';
}

void StructuredTracer::write_fallback_serialization_error(
    std::ostream &output_stream) noexcept {
  try {
    static constexpr char k_fallback_record[] =
        "{\"timestamp\":\"1970-01-01T00:00:00.000Z\",\"level\":\"error\","
        "\"module\":\"monitoring\",\"info\":\"trace_serialization_failed\"}\n";

    std::streambuf *stream_buffer = output_stream.rdbuf();
    if (stream_buffer != nullptr) {
      (void)stream_buffer->sputn(k_fallback_record,
                                 sizeof(k_fallback_record) - 1U);
      (void)stream_buffer->pubsync();
    }
  } catch (...) {
    // Best-effort fallback only.
  }
}

} // namespace mqtt
