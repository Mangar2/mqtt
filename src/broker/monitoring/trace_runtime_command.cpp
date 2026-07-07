/**
 * @file trace_runtime_command.cpp
 * @brief Runtime trace configuration command parsing (Module 26.4).
 */

#include "broker/monitoring/trace_runtime_command.h"

#include "helper/string_helper.h"

#include <string>
#include <string_view>

#include "broker/monitoring/trace_level.h"

namespace mqtt {

namespace {

[[nodiscard]] std::string binary_to_string(const BinaryData &payload) {
  return std::string(payload.data.begin(), payload.data.end());
}

[[nodiscard]] bool parse_module_override_payload(std::string_view payload,
                                                 bool &enable_trace) {
  const std::string normalised = mqtt::helper::trim(payload);
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
        parse_trace_level(mqtt::helper::trim(payload_text));
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
