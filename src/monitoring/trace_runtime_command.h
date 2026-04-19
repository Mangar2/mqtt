#pragma once

/**
 * @file trace_runtime_command.h
 * @brief Runtime trace configuration command parsing for $SYS messages
 *        (Module 26.4).
 */

#include "data_model/message/message.h"
#include "monitoring/structured_tracer.h"

namespace mqtt {

/**
 * @brief Apply a runtime tracing configuration message to a tracer.
 *
 * Supported topics:
 * - $SYS/broker/tracing/global with payload none|error|warning|info|trace
 * - $SYS/broker/tracing/module/<module> with payload trace|none|on|off
 *
 * Unknown topics or invalid payloads are ignored.
 *
 * @param tracer Structured tracer to update.
 * @param message System message carrying runtime tracing configuration.
 */
void apply_trace_runtime_command(StructuredTracer &tracer,
                                 const Message &message);

} // namespace mqtt
