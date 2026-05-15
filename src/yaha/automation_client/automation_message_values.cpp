#include "yaha/automation_client/automation_message_values.h"

#include <sstream>

namespace yaha::automation_message_values {

ExpressionEvaluator::Value toExpressionValue(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }
    return std::get<double>(messageValue);
}

std::string valueToLogText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return textStream.str();
}

std::string qosToLogText(const Qos qosValue) {
    switch (qosValue) {
    case Qos::AtMostOnce:
        return "0";
    case Qos::AtLeastOnce:
        return "1";
    case Qos::ExactlyOnce:
        return "2";
    }

    return "unknown";
}

} // namespace yaha::automation_message_values
