/**
 * @file trace_runtime_command.cpp
 * @brief Runtime trace configuration command parsing (Module 26.4).
 */

#include "monitoring/trace_runtime_command.h"

#include <cctype>
#include <string>
#include <string_view>

#include "monitoring/trace_level.h"

namespace mqtt {

namespace {

[[nodiscard]] std::string binary_to_string(const BinaryData &payload) {
  return std::string(payload.data.begin(), payload.data.end());
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t start_index = 0U;
  while (start_index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start_index])) != 0) {
    ++start_index;
  }

  std::size_t end_index = text.size();
  while (end_index > start_index &&
         std::isspace(static_cast<unsigned char>(text[end_index - 1U])) != 0) {
    --end_index;
  }

  return std::string(text.substr(start_index, end_index - start_index));
}

[[nodiscard]] bool parse_module_override_payload(std::string_view payload,
                                                 bool &enable_trace) {
  const std::string normalised = trim_copy(payload);
  if (normalised == "trace" || normalised == "on") {
    enable_trace = true;
    return true;
  }
  if (normalised == "none" || normalised == "off") {
    enable_trace = false;
    return true;
  }
  return false;
}

constexpr std::string_view k_trace_global_topic =
    "$SYS/broker/tracing/global";
constexpr std::string_view k_trace_module_prefix =
    "$SYS/broker/tracing/module/";

} // namespace

void apply_trace_runtime_command(StructuredTracer &tracer,
                                 const Message &message) {
  const std::string &topic_name = message.topic.value;
  const std::string payload_text = binary_to_string(message.payload);

  if (topic_name == k_trace_global_topic) {
    const std::optional<TraceLevel> parsed_level =
        parse_trace_level(trim_copy(payload_text));
    if (parsed_level.has_value()) {
      tracer.set_global_level(*parsed_level);
    }
    return;
  }

  if (!topic_name.starts_with(k_trace_module_prefix)) {
    return;
  }

  const std::string module_name = topic_name.substr(k_trace_module_prefix.size());
  if (module_name.empty()) {
    return;
  }

  bool enable_trace = false;
  if (!parse_module_override_payload(payload_text, enable_trace)) {
    return;
  }

  if (enable_trace) {
    tracer.enable_trace_module(module_name);
    return;
  }

  tracer.disable_trace_module(module_name);
}

} // namespace mqtt
