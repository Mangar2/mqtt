#pragma once

/**
 * @file automation_message_values.h
 * @brief Conversion and text helpers for MQTT message values.
 */

#include <string>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/message/message.h"

namespace yaha::automation_message_values {

[[nodiscard]] ExpressionEvaluator::Value toExpressionValue(const Value& messageValue);
[[nodiscard]] std::string valueToLogText(const Value& messageValue);
[[nodiscard]] std::string qosToLogText(Qos qosValue);

} // namespace yaha::automation_message_values
