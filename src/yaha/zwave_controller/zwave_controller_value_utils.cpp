#include "yaha/zwave_controller/zwave_controller_value_utils.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace yaha::zwave_controller_value_utils {
namespace {

constexpr double kIntegerTolerance = 1e-9;
constexpr unsigned char kJsonControlThreshold = 0x20U;

const std::regex& iso8601TimestampRegex() {
    static const std::regex regex{
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+\-]\d{2}:\d{2})$)",
        std::regex::ECMAScript};
    return regex;
}

[[nodiscard]] bool valueAsBool(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value); text != nullptr) {
        return *text == "on" || *text == "1" || *text == "true";
    }
    return std::fabs(std::get<double>(value)) >= kIntegerTolerance;
}

[[nodiscard]] bool isSpecCompliantReasonTimestamp(const std::string& timestamp) {
    if (timestamp.empty()) {
        return false;
    }
    return std::regex_match(timestamp, iso8601TimestampRegex());
}

[[nodiscard]] std::string sanitizeReasonMessageForJson(std::string text) {
    for (char& character : text) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        if (unsignedCharacter < kJsonControlThreshold && character != '\n' && character != '\r' && character != '\t') {
            character = ' ';
        }
    }
    return text;
}

} // namespace

Value applySwitchOutboundConversion(const Value& value, const std::string& typeName) {
    if (typeName != "switch") {
        return value;
    }

    return valueAsBool(value) ? Value{std::string{"on"}} : Value{std::string{"off"}};
}

std::optional<bool> valueAsSemanticBool(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        if (std::fabs(*numericValue) < kIntegerTolerance) {
            return false;
        }
        if (std::isfinite(*numericValue)) {
            return true;
        }
        return std::nullopt;
    }

    std::string normalized = std::get<std::string>(value);
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (normalized == "on" || normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "off" || normalized == "false" || normalized == "0") {
        return false;
    }

    return std::nullopt;
}

std::string valueToDebugText(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        std::ostringstream stream{};
        stream << *numericValue;
        return stream.str();
    }
    return std::get<std::string>(value);
}

void addSpecCompliantReason(Message& message, const ReasonEntry& reasonEntry) {
    const std::string sanitizedMessage = sanitizeReasonMessageForJson(reasonEntry.message);
    if (sanitizedMessage.empty()) {
        return;
    }

    if (isSpecCompliantReasonTimestamp(reasonEntry.timestamp)) {
        message.addReason(sanitizedMessage, reasonEntry.timestamp);
        return;
    }

    message.addReason(sanitizedMessage);
}

double valueAsDouble(const Value& value) {
    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        return *numericValue;
    }

    std::size_t parsedChars = 0U;
    const auto& text = std::get<std::string>(value);
    const double parsed = std::stod(text, &parsedChars);
    if (parsedChars != text.size()) {
        throw std::runtime_error("invalid numeric value '" + text + "'");
    }
    return parsed;
}

bool valuesEquivalent(const Value& leftValue, const Value& rightValue) {
    if (const auto* leftText = std::get_if<std::string>(&leftValue); leftText != nullptr) {
        const auto* rightText = std::get_if<std::string>(&rightValue);
        return rightText != nullptr && *leftText == *rightText;
    }

    const auto* leftNumber = std::get_if<double>(&leftValue);
    const auto* rightNumber = std::get_if<double>(&rightValue);
    if (leftNumber == nullptr || rightNumber == nullptr) {
        const std::optional<bool> leftSemanticBool = valueAsSemanticBool(leftValue);
        const std::optional<bool> rightSemanticBool = valueAsSemanticBool(rightValue);
        return leftSemanticBool.has_value() && rightSemanticBool.has_value()
            && *leftSemanticBool == *rightSemanticBool;
    }

    return std::fabs(*leftNumber - *rightNumber) < kIntegerTolerance;
}

Value writeValueToExpectedValue(const ZwaveWriteRequest& writeRequest) {
    if (const auto* boolValue = std::get_if<bool>(&writeRequest.value); boolValue != nullptr) {
        return Value{*boolValue ? 1.0 : 0.0};
    }

    if (const auto* numericValue = std::get_if<double>(&writeRequest.value); numericValue != nullptr) {
        return Value{*numericValue};
    }

    return Value{std::get<std::string>(writeRequest.value)};
}

Value toExpectedOutboundValue(const Value& value, const std::string& typeName) {
    return applySwitchOutboundConversion(value, typeName);
}

} // namespace yaha::zwave_controller_value_utils
