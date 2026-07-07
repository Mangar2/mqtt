#pragma once

#include "yaha/message/message.h"
#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <string>

namespace yaha::zwave_controller_value_utils {

[[nodiscard]] Value applySwitchOutboundConversion(const Value& value, const std::string& typeName);
[[nodiscard]] std::optional<bool> valueAsSemanticBool(const Value& value);
[[nodiscard]] std::string valueToDebugText(const Value& value);
void addSpecCompliantReason(Message& message, const ReasonEntry& reasonEntry);
[[nodiscard]] double valueAsDouble(const Value& value);
[[nodiscard]] bool valuesEquivalent(const Value& leftValue, const Value& rightValue);
[[nodiscard]] Value writeValueToExpectedValue(const ZwaveWriteRequest& writeRequest);
[[nodiscard]] Value toExpectedOutboundValue(const Value& value, const std::string& typeName);

} // namespace yaha::zwave_controller_value_utils
