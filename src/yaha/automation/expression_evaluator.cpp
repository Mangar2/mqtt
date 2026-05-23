#include "yaha/automation/expression_evaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <optional>
#include <sstream>
#include <vector>

namespace yaha {
namespace {

constexpr int k_max_minutes_or_seconds{59};
constexpr int k_max_hours{23};
constexpr int k_decimal_base{10};
constexpr int k_millis_scale_one_digit{100};
constexpr int k_millis_scale_two_digits{10};
constexpr int k_seconds_per_hour{3600};
constexpr int k_seconds_per_minute{60};
constexpr double k_numeric_epsilon{1e-12};

constexpr std::size_t k_iso_min_length{20U};
constexpr std::size_t k_iso_year_index{0U};
constexpr std::size_t k_iso_month_index{5U};
constexpr std::size_t k_iso_day_index{8U};
constexpr std::size_t k_iso_hour_index{11U};
constexpr std::size_t k_iso_minute_index{14U};
constexpr std::size_t k_iso_second_index{17U};
constexpr std::size_t k_iso_date_separator_1_index{4U};
constexpr std::size_t k_iso_date_separator_2_index{7U};
constexpr std::size_t k_iso_time_separator_index{10U};
constexpr std::size_t k_iso_hour_separator_index{13U};
constexpr std::size_t k_iso_minute_separator_index{16U};
constexpr std::size_t k_iso_fraction_start_index{19U};
constexpr std::size_t k_iso_year_length{4U};
constexpr std::size_t k_iso_two_digit_length{2U};
constexpr std::size_t k_iso_max_millis_digits{3U};

using ExternalVariableMap = ExpressionEvaluator::VariableMap;

struct MapRuntimeEntry {
    bool isDefault{false};
    std::string keyToken;
    ExpressionEvaluator::Value value;
};

struct MapRuntimeValue {
    std::vector<MapRuntimeEntry> entries;
};

using RuntimeValue = std::variant<std::string, double, bool, std::chrono::system_clock::time_point, MapRuntimeValue>;

struct EvaluatedNode {
    RuntimeValue value;
    std::string reason;
};

struct IsoInstantComponents {
    int yearValue{0};
    int monthValue{0};
    int dayValue{0};
    int hourValue{0};
    int minuteValue{0};
    int secondValue{0};
    int millisecondValue{0};
    int timezoneOffsetSeconds{0};
};

[[nodiscard]] std::string toLower(std::string textValue) {
    std::ranges::transform(textValue, textValue.begin(), [](const unsigned char charValue) {
        return static_cast<char>(std::tolower(charValue));
    });
    return textValue;
}

[[nodiscard]] bool parseDouble(const std::string& tokenText, double* parsedValue) {
    std::size_t parsedLength = 0U;
    try {
        *parsedValue = std::stod(tokenText, &parsedLength);
    } catch (...) {
        return false;
    }
    return parsedLength == tokenText.size();
}

[[nodiscard]] bool parseFixedUnsignedInt(
    const std::string& textValue,
    const std::size_t startIndex,
    const std::size_t length,
    int* parsedValue) {
    if (startIndex + length > textValue.size() || length == 0U) {
        return false;
    }

    int value = 0;
    for (std::size_t index = 0U; index < length; ++index) {
        const char currentChar = textValue[startIndex + index];
        if (currentChar < '0' || currentChar > '9') {
            return false;
        }
        value = value * k_decimal_base + (currentChar - '0');
    }

    *parsedValue = value;
    return true;
}

[[nodiscard]] bool parseIsoDateTimePrefix(
    const std::string& textValue,
    IsoInstantComponents* components,
    std::size_t* cursorIndex) {
    if (textValue.size() < k_iso_min_length) {
        return false;
    }
    if (textValue[k_iso_date_separator_1_index] != '-'
        || textValue[k_iso_date_separator_2_index] != '-'
        || textValue[k_iso_time_separator_index] != 'T'
        || textValue[k_iso_hour_separator_index] != ':'
        || textValue[k_iso_minute_separator_index] != ':') {
        return false;
    }

    if (!parseFixedUnsignedInt(textValue, k_iso_year_index, k_iso_year_length, &components->yearValue)
        || !parseFixedUnsignedInt(textValue, k_iso_month_index, k_iso_two_digit_length, &components->monthValue)
        || !parseFixedUnsignedInt(textValue, k_iso_day_index, k_iso_two_digit_length, &components->dayValue)
        || !parseFixedUnsignedInt(textValue, k_iso_hour_index, k_iso_two_digit_length, &components->hourValue)
        || !parseFixedUnsignedInt(textValue, k_iso_minute_index, k_iso_two_digit_length, &components->minuteValue)
        || !parseFixedUnsignedInt(textValue, k_iso_second_index, k_iso_two_digit_length, &components->secondValue)) {
        return false;
    }

    if (components->hourValue < 0 || components->hourValue > k_max_hours
        || components->minuteValue < 0 || components->minuteValue > k_max_minutes_or_seconds
        || components->secondValue < 0 || components->secondValue > k_max_minutes_or_seconds) {
        return false;
    }

    *cursorIndex = k_iso_fraction_start_index;
    return true;
}

[[nodiscard]] bool parseIsoOptionalFraction(
    const std::string& textValue,
    std::size_t* cursorIndex,
    IsoInstantComponents* components) {
    if (*cursorIndex >= textValue.size() || textValue[*cursorIndex] != '.') {
        return true;
    }

    *cursorIndex += 1U;
    const std::size_t fractionStart = *cursorIndex;
    while (*cursorIndex < textValue.size() && std::isdigit(static_cast<unsigned char>(textValue[*cursorIndex])) != 0) {
        *cursorIndex += 1U;
    }

    const std::size_t fractionLength = *cursorIndex - fractionStart;
    if (fractionLength == 0U) {
        return false;
    }

    int fractionAccumulator = 0;
    const std::size_t digitsToUse = std::min<std::size_t>(k_iso_max_millis_digits, fractionLength);
    for (std::size_t index = 0U; index < digitsToUse; ++index) {
        fractionAccumulator = fractionAccumulator * k_decimal_base + (textValue[fractionStart + index] - '0');
    }

    if (digitsToUse == 1U) {
        fractionAccumulator *= k_millis_scale_one_digit;
    } else if (digitsToUse == 2U) {
        fractionAccumulator *= k_millis_scale_two_digits;
    }

    components->millisecondValue = fractionAccumulator;
    return true;
}

[[nodiscard]] bool parseIsoTimezoneOffset(
    const std::string& textValue,
    std::size_t* cursorIndex,
    IsoInstantComponents* components) {
    if (*cursorIndex >= textValue.size()) {
        return false;
    }

    if (textValue[*cursorIndex] == 'Z') {
        *cursorIndex += 1U;
        components->timezoneOffsetSeconds = 0;
        return true;
    }

    if (textValue[*cursorIndex] != '+' && textValue[*cursorIndex] != '-') {
        return false;
    }

    const bool isNegativeOffset = textValue[*cursorIndex] == '-';
    *cursorIndex += 1U;

    int offsetHourValue = 0;
    int offsetMinuteValue = 0;
    if (!parseFixedUnsignedInt(textValue, *cursorIndex, k_iso_two_digit_length, &offsetHourValue)) {
        return false;
    }
    *cursorIndex += k_iso_two_digit_length;
    if (*cursorIndex >= textValue.size() || textValue[*cursorIndex] != ':') {
        return false;
    }
    *cursorIndex += 1U;
    if (!parseFixedUnsignedInt(textValue, *cursorIndex, k_iso_two_digit_length, &offsetMinuteValue)) {
        return false;
    }
    *cursorIndex += k_iso_two_digit_length;

    components->timezoneOffsetSeconds = offsetHourValue * k_seconds_per_hour + offsetMinuteValue * k_seconds_per_minute;
    if (isNegativeOffset) {
        components->timezoneOffsetSeconds = -components->timezoneOffsetSeconds;
    }
    return true;
}

[[nodiscard]] bool tryParseIsoInstantText(
    const std::string& textValue,
    std::chrono::system_clock::time_point* parsedTimePoint) {
    IsoInstantComponents components;
    std::size_t cursorIndex = 0U;
    if (!parseIsoDateTimePrefix(textValue, &components, &cursorIndex)) {
        return false;
    }
    if (!parseIsoOptionalFraction(textValue, &cursorIndex, &components)) {
        return false;
    }
    if (!parseIsoTimezoneOffset(textValue, &cursorIndex, &components)) {
        return false;
    }

    if (cursorIndex != textValue.size()) {
        return false;
    }

    const auto yearMonthDay = std::chrono::year{components.yearValue}
        / std::chrono::month{static_cast<unsigned>(components.monthValue)}
        / std::chrono::day{static_cast<unsigned>(components.dayValue)};
    if (!yearMonthDay.ok()) {
        return false;
    }

    const auto dayStart = std::chrono::sys_days{yearMonthDay};
    auto timePoint = std::chrono::system_clock::time_point{dayStart}
        + std::chrono::hours{components.hourValue}
        + std::chrono::minutes{components.minuteValue}
        + std::chrono::seconds{components.secondValue}
        + std::chrono::milliseconds{components.millisecondValue};

    timePoint -= std::chrono::seconds{components.timezoneOffsetSeconds};
    *parsedTimePoint = timePoint;
    return true;
}

[[nodiscard]] std::string trimWhitespace(const std::string& textValue) {
    std::size_t beginIndex = 0U;
    while (beginIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[beginIndex])) != 0) {
        beginIndex += 1U;
    }

    std::size_t endIndex = textValue.size();
    while (endIndex > beginIndex
        && std::isspace(static_cast<unsigned char>(textValue[endIndex - 1U])) != 0) {
        endIndex -= 1U;
    }

    return textValue.substr(beginIndex, endIndex - beginIndex);
}

[[nodiscard]] std::optional<double> tryNumericLikeJs(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<double>(runtimeValue)) {
        return std::get<double>(runtimeValue);
    }
    if (!std::holds_alternative<std::string>(runtimeValue)) {
        return std::nullopt;
    }

    const std::string trimmedText = trimWhitespace(std::get<std::string>(runtimeValue));
    if (trimmedText.empty()) {
        return std::nullopt;
    }

    double parsedValue = 0.0;
    if (!parseDouble(trimmedText, &parsedValue)) {
        return std::nullopt;
    }
    return parsedValue;
}

[[nodiscard]] bool tryParseTimeText(const std::string& timeText, std::chrono::seconds* parsedSeconds) {
    int hourValue = -1;
    int minuteValue = -1;
    int secondValue = 0;

    if (std::sscanf(timeText.c_str(), "%d:%d:%d", &hourValue, &minuteValue, &secondValue) == 3) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds || secondValue < 0
            || secondValue > k_max_minutes_or_seconds) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue} + std::chrono::seconds{secondValue};
        return true;
    }

    if (std::sscanf(timeText.c_str(), "%d:%d", &hourValue, &minuteValue) == 2) {
        if (hourValue < 0 || minuteValue < 0 || minuteValue > k_max_minutes_or_seconds) {
            return false;
        }
        *parsedSeconds = std::chrono::hours{hourValue} + std::chrono::minutes{minuteValue};
        return true;
    }

    return false;
}

[[nodiscard]] std::string formatTimeText(const std::chrono::seconds secondsValue) {
    const auto secondCount = secondsValue.count();
    const auto normalizedSeconds = (secondCount % 86400 + 86400) % 86400;
    const int hourValue = static_cast<int>(normalizedSeconds / 3600);
    const int minuteValue = static_cast<int>((normalizedSeconds % 3600) / 60);
    const int secondValue = static_cast<int>(normalizedSeconds % 60);

    std::ostringstream textStream;
    textStream.fill('0');
    textStream.width(2);
    textStream << hourValue << ':';
    textStream.width(2);
    textStream << minuteValue << ':';
    textStream.width(2);
    textStream << secondValue;
    return textStream.str();
}

[[nodiscard]] std::optional<std::tm> toLocalCalendarTime(
    const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    if (localtime_s(&localCalendarTime, &epochSeconds) != 0) {
        return std::nullopt;
    }
#else
    if (localtime_r(&epochSeconds, &localCalendarTime) == nullptr) {
        return std::nullopt;
    }
#endif
    return localCalendarTime;
}

[[nodiscard]] std::optional<std::chrono::seconds> localTimeOfDay(
    const std::chrono::system_clock::time_point& timePoint) {
    const auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    return std::chrono::hours{localCalendarTime->tm_hour}
        + std::chrono::minutes{localCalendarTime->tm_min}
        + std::chrono::seconds{localCalendarTime->tm_sec};
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point> localDayStart(
    const std::chrono::system_clock::time_point& timePoint) {
    auto localCalendarTime = toLocalCalendarTime(timePoint);
    if (!localCalendarTime.has_value()) {
        return std::nullopt;
    }

    localCalendarTime->tm_hour = 0;
    localCalendarTime->tm_min = 0;
    localCalendarTime->tm_sec = 0;
    localCalendarTime->tm_isdst = -1;
    const std::time_t localMidnightSeconds = std::mktime(&*localCalendarTime);
    if (localMidnightSeconds == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(localMidnightSeconds);
}

[[nodiscard]] std::optional<std::chrono::seconds> tryTimeOfDay(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        return localTimeOfDay(std::get<std::chrono::system_clock::time_point>(runtimeValue));
    }

    if (std::holds_alternative<std::string>(runtimeValue)) {
        const std::string stringValue = std::get<std::string>(runtimeValue);
        std::chrono::seconds parsedSeconds{};
        if (tryParseTimeText(stringValue, &parsedSeconds)) {
            return parsedSeconds;
        }

        std::chrono::system_clock::time_point parsedTimePoint{};
        if (tryParseIsoInstantText(stringValue, &parsedTimePoint)) {
            return localTimeOfDay(parsedTimePoint);
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::string valueToString(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::string>(runtimeValue)) {
        return std::get<std::string>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        std::ostringstream textStream;
        textStream << std::get<double>(runtimeValue);
        return textStream.str();
    }
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue) ? "true" : "false";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        const auto secondsValue = tryTimeOfDay(runtimeValue);
        return secondsValue.has_value() ? formatTimeText(*secondsValue) : "time";
    }
    return "map";
}

[[nodiscard]] std::string valueTypeToString(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<std::string>(runtimeValue)) {
        return "string";
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        return "number";
    }
    if (std::holds_alternative<bool>(runtimeValue)) {
        return "bool";
    }
    if (std::holds_alternative<std::chrono::system_clock::time_point>(runtimeValue)) {
        return "time";
    }
    return "map";
}

[[nodiscard]] bool isUndefinedReason(const std::string& reasonText) {
    constexpr std::string_view undefinedSuffix{" (undefined)"};
    return reasonText.ends_with(undefinedSuffix);
}

[[nodiscard]] std::string extractUndefinedVariableName(const std::string& reasonText) {
    constexpr std::string_view undefinedSuffix{" (undefined)"};
    if (!isUndefinedReason(reasonText)) {
        return {};
    }
    return reasonText.substr(0U, reasonText.size() - undefinedSuffix.size());
}

[[nodiscard]] std::string relationOperatorSymbol(const std::string& relationOperator) {
    if (relationOperator == "gt") {
        return ">";
    }
    if (relationOperator == "lt") {
        return "<";
    }
    if (relationOperator == "ge") {
        return ">=";
    }
    if (relationOperator == "le") {
        return "<=";
    }
    return "?";
}

template <typename ComparableType>
[[nodiscard]] bool evaluateRelation(
    const ComparableType& leftValue,
    const ComparableType& rightValue,
    const std::string& relationOperator) {
    if (relationOperator == "gt") {
        return leftValue > rightValue;
    }
    if (relationOperator == "lt") {
        return leftValue < rightValue;
    }
    if (relationOperator == "ge") {
        return leftValue >= rightValue;
    }
    return leftValue <= rightValue;
}

void appendUndefinedCause(
    std::ostringstream* errorText,
    const std::string& leftUndefinedVariable,
    const std::string& rightUndefinedVariable) {
    if (leftUndefinedVariable.empty() && rightUndefinedVariable.empty()) {
        *errorText << "; because operands are neither both numeric nor both time";
        return;
    }

    *errorText << "; because undefined external variable(s): ";
    std::string separator;
    if (!leftUndefinedVariable.empty()) {
        *errorText << leftUndefinedVariable;
        separator = ", ";
    }
    if (!rightUndefinedVariable.empty()) {
        *errorText << separator << rightUndefinedVariable;
    }
}

[[nodiscard]] bool toBool(const RuntimeValue& runtimeValue) {
    if (std::holds_alternative<bool>(runtimeValue)) {
        return std::get<bool>(runtimeValue);
    }
    if (std::holds_alternative<double>(runtimeValue)) {
        return std::fabs(std::get<double>(runtimeValue)) > k_numeric_epsilon;
    }
    if (std::holds_alternative<std::string>(runtimeValue)) {
        const std::string loweredValue = toLower(std::get<std::string>(runtimeValue));
        if (loweredValue == "" || loweredValue == "false" || loweredValue == "off" || loweredValue == "0") {
            return false;
        }
        if (loweredValue == "true" || loweredValue == "on" || loweredValue == "1") {
            return true;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool evalEquals(const RuntimeValue& leftValue, const RuntimeValue& rightValue) {
    if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
        return std::fabs(std::get<double>(leftValue) - std::get<double>(rightValue)) < k_numeric_epsilon;
    }

    const auto leftTime = tryTimeOfDay(leftValue);
    const auto rightTime = tryTimeOfDay(rightValue);
    if (leftTime.has_value() && rightTime.has_value()) {
        return *leftTime == *rightTime;
    }

    return valueToString(leftValue) == valueToString(rightValue);
}

class RuntimeEvaluator {
public:
    RuntimeEvaluator(const FieldScriptAst& scriptAst, const ExternalVariableMap& variableMap)
        : script_(scriptAst), variables_(variableMap) {
        for (const auto& declaration : script_.declarations) {
            declarations_.insert({declaration.name, declaration.entries});
        }
    }

    [[nodiscard]] ExpressionEvaluationResult run() {
        ExpressionEvaluationResult result;

        if (!script_.resultExpression) {
            result.errors.emplace_back("missing result expression");
            result.usedVariables = usedVariables_;
            return result;
        }

        const auto evaluatedNode = eval(script_.resultExpression);
        if (!evaluatedNode.has_value()) {
            result.errors = errors_;
            result.usedVariables = usedVariables_;
            return result;
        }

        if (std::holds_alternative<std::string>(evaluatedNode->value)) {
            result.value = std::get<std::string>(evaluatedNode->value);
        } else if (std::holds_alternative<double>(evaluatedNode->value)) {
            result.value = std::get<double>(evaluatedNode->value);
        } else if (std::holds_alternative<bool>(evaluatedNode->value)) {
            result.value = std::get<bool>(evaluatedNode->value);
        } else if (std::holds_alternative<std::chrono::system_clock::time_point>(evaluatedNode->value)) {
            result.value = std::get<std::chrono::system_clock::time_point>(evaluatedNode->value);
        } else {
            errors_.emplace_back("result expression must not evaluate to a map");
        }

        result.reason = evaluatedNode->reason;
        if (!missingVariables_.empty()) {
            result.value = false;
            result.reason = buildMissingVariablesReason();
        }
        result.success = errors_.empty();
        result.errors = errors_;
        result.usedVariables = usedVariables_;
        return result;
    }

private:
    [[nodiscard]] std::optional<EvaluatedNode> eval(const ExprPtr& expression) {
        if (!expression) {
            errors_.emplace_back("invalid null expression");
            return std::nullopt;
        }

        if (std::holds_alternative<LiteralNode>(expression->node)) {
            return evalLiteralNode(std::get<LiteralNode>(expression->node));
        }

        if (std::holds_alternative<IdentifierNode>(expression->node)) {
            return evalIdentifierNode(std::get<IdentifierNode>(expression->node));
        }

        if (std::holds_alternative<VariableRefNode>(expression->node)) {
            return evalVariableRefNode(std::get<VariableRefNode>(expression->node));
        }

        if (std::holds_alternative<UnaryOpNode>(expression->node)) {
            return evalUnaryNode(std::get<UnaryOpNode>(expression->node));
        }

        if (std::holds_alternative<BinaryOpNode>(expression->node)) {
            return evalBinaryNode(std::get<BinaryOpNode>(expression->node));
        }

        if (std::holds_alternative<IfCallNode>(expression->node)) {
            return evalIfCallNode(std::get<IfCallNode>(expression->node));
        }

        if (std::holds_alternative<MapLiteralNode>(expression->node)) {
            return evalMapLiteralNode(std::get<MapLiteralNode>(expression->node));
        }

        if (std::holds_alternative<MapCallNode>(expression->node)) {
            return evalMapCallNode(std::get<MapCallNode>(expression->node));
        }

        errors_.emplace_back("unsupported expression node");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalLiteralNode(const LiteralNode& literalNode) {
        const auto& literalValue = literalNode.value;
        if (std::holds_alternative<std::string>(literalValue)) {
            const std::string textValue = std::get<std::string>(literalValue);
            if (textValue.find('/') != std::string::npos) {
                usedVariables_.insert(textValue);
                const auto variableIterator = variables_.find(textValue);
                if (variableIterator != variables_.end()) {
                    if (std::holds_alternative<std::string>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<std::string>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    if (std::holds_alternative<double>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<double>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    if (std::holds_alternative<bool>(variableIterator->second)) {
                        const RuntimeValue variableValue{std::get<bool>(variableIterator->second)};
                        return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                    }
                    const RuntimeValue variableValue{
                        std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
                    return EvaluatedNode{.value = variableValue, .reason = textValue + " (" + valueToString(variableValue) + ")"};
                }

                missingVariables_.insert(textValue);
                return EvaluatedNode{.value = RuntimeValue{false}, .reason = textValue + " (undefined)"};
            }
            return EvaluatedNode{.value = RuntimeValue{textValue}, .reason = textValue};
        }

        const RuntimeValue numberValue{std::get<double>(literalValue)};
        return EvaluatedNode{.value = numberValue, .reason = valueToString(numberValue)};
    }

    [[nodiscard]] static std::optional<EvaluatedNode> evalIdentifierNode(const IdentifierNode& identifierNode) {
        const std::string identifierName = identifierNode.name;
        if (toLower(identifierName) == "true") {
            return EvaluatedNode{.value = RuntimeValue{true}, .reason = "constant true"};
        }
        if (toLower(identifierName) == "false") {
            return EvaluatedNode{.value = RuntimeValue{false}, .reason = "constant false"};
        }
        return EvaluatedNode{.value = RuntimeValue{identifierName}, .reason = identifierName};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalVariableRefNode(const VariableRefNode& variableNode) {
        const std::string variableName = variableNode.name;
        usedVariables_.insert(variableName);
        const auto variableIterator = variables_.find(variableName);
        if (variableIterator == variables_.end()) {
            missingVariables_.insert(variableName);
            return EvaluatedNode{.value = RuntimeValue{false}, .reason = variableName + " (undefined)"};
        }

        if (std::holds_alternative<std::string>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<std::string>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        if (std::holds_alternative<double>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<double>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        if (std::holds_alternative<bool>(variableIterator->second)) {
            const RuntimeValue variableValue{std::get<bool>(variableIterator->second)};
            return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
        }
        const RuntimeValue variableValue{std::get<std::chrono::system_clock::time_point>(variableIterator->second)};
        return EvaluatedNode{.value = variableValue, .reason = variableName + " (" + valueToString(variableValue) + ")"};
    }

    [[nodiscard]] std::string buildMissingVariablesReason() const {
        std::string reasonText{"false, undefined variables: "};
        std::string separator;
        for (const auto& variableName : missingVariables_) {
            reasonText.append(separator);
            reasonText.append(variableName);
            separator = ", ";
        }
        return reasonText;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalUnaryNode(const UnaryOpNode& unaryNode) {
        const auto operandNode = eval(unaryNode.operand);
        if (!operandNode.has_value()) {
            return std::nullopt;
        }
        const RuntimeValue unaryValue{!toBool(operandNode->value)};
        return EvaluatedNode{.value = unaryValue, .reason = "not (" + operandNode->reason + ")"};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalBinaryNode(const BinaryOpNode& binaryNode) {
        const auto leftNode = eval(binaryNode.left);
        const auto rightNode = eval(binaryNode.right);
        if (!leftNode.has_value() || !rightNode.has_value()) {
            return std::nullopt;
        }

        switch (binaryNode.op) {
        case BinaryOperator::And: {
            const bool leftBool = toBool(leftNode->value);
            const bool rightBool = toBool(rightNode->value);
            const bool combinedValue = leftBool && rightBool;
            std::string combinedReason;
            if (combinedValue) {
                if (!leftNode->reason.empty() && !rightNode->reason.empty()) {
                    combinedReason = leftNode->reason + " and " + rightNode->reason;
                } else {
                    combinedReason = leftNode->reason + rightNode->reason;
                }
            } else {
                combinedReason = leftBool ? rightNode->reason : leftNode->reason;
            }
            return EvaluatedNode{.value = RuntimeValue{combinedValue}, .reason = combinedReason};
        }
        case BinaryOperator::Or: {
            const bool leftBool = toBool(leftNode->value);
            const bool rightBool = toBool(rightNode->value);
            const bool combinedValue = leftBool || rightBool;
            std::string combinedReason;
            if (combinedValue) {
                combinedReason = leftBool ? leftNode->reason : rightNode->reason;
            } else if (!leftNode->reason.empty() && !rightNode->reason.empty()) {
                combinedReason = leftNode->reason + " or " + rightNode->reason;
            } else {
                combinedReason = leftNode->reason + rightNode->reason;
            }
            return EvaluatedNode{.value = RuntimeValue{combinedValue}, .reason = combinedReason};
        }
        case BinaryOperator::Eq:
            return EvaluatedNode{
                .value = RuntimeValue{evalEquals(leftNode->value, rightNode->value)},
                .reason = leftNode->reason + " is = " + rightNode->reason};
        case BinaryOperator::Neq:
            return EvaluatedNode{
                .value = RuntimeValue{!evalEquals(leftNode->value, rightNode->value)},
                .reason = leftNode->reason + " is != " + rightNode->reason};
        case BinaryOperator::Gt:
            return compareRel(*leftNode, *rightNode, "gt");
        case BinaryOperator::Lt:
            return compareRel(*leftNode, *rightNode, "lt");
        case BinaryOperator::Ge:
            return compareRel(*leftNode, *rightNode, "ge");
        case BinaryOperator::Le:
            return compareRel(*leftNode, *rightNode, "le");
        case BinaryOperator::Add:
        case BinaryOperator::Sub:
            return evalAddSub(*leftNode, *rightNode, binaryNode.op == BinaryOperator::Add);
        }

        errors_.emplace_back("unsupported binary operator");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalIfCallNode(const IfCallNode& ifCallNode) {
        const auto conditionNode = eval(ifCallNode.condition);
        if (!conditionNode.has_value()) {
            return std::nullopt;
        }
        if (toBool(conditionNode->value)) {
            const auto trueNode = eval(ifCallNode.trueValue);
            if (!trueNode.has_value()) {
                return std::nullopt;
            }
            return EvaluatedNode{.value = trueNode->value, .reason = "if " + conditionNode->reason + " then " + trueNode->reason};
        }

        const auto falseNode = eval(ifCallNode.falseValue);
        if (!falseNode.has_value()) {
            return std::nullopt;
        }
        return EvaluatedNode{.value = falseNode->value, .reason = "if " + conditionNode->reason + ": " + falseNode->reason};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalMapLiteralNode(const MapLiteralNode& mapNode) {
        MapRuntimeValue runtimeMap;
        for (const auto& mapEntry : mapNode.entries) {
            const auto entryNode = eval(mapEntry.value);
            if (!entryNode.has_value()) {
                return std::nullopt;
            }
            if (std::holds_alternative<MapRuntimeValue>(entryNode->value)) {
                errors_.emplace_back("map entry value must not be a map");
                return std::nullopt;
            }

            if (std::holds_alternative<std::string>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::string>(entryNode->value)});
            } else if (std::holds_alternative<double>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<double>(entryNode->value)});
            } else if (std::holds_alternative<bool>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<bool>(entryNode->value)});
            } else if (std::holds_alternative<std::chrono::system_clock::time_point>(entryNode->value)) {
                runtimeMap.entries.push_back(MapRuntimeEntry{.isDefault = mapEntry.key.isDefault, .keyToken = mapEntry.key.token, .value = std::get<std::chrono::system_clock::time_point>(entryNode->value)});
            }
        }

        return EvaluatedNode{.value = RuntimeValue{runtimeMap}, .reason = "map literal"};
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalMapCallNode(const MapCallNode& mapCallNode) {
        const auto declarationIterator = declarations_.find(mapCallNode.name);
        if (declarationIterator == declarations_.end()) {
            errors_.emplace_back("undefined map declaration: " + mapCallNode.name);
            return std::nullopt;
        }

        const auto selectorNode = eval(mapCallNode.selector);
        if (!selectorNode.has_value()) {
            return std::nullopt;
        }

        std::optional<EvaluatedNode> defaultValue;
        for (const auto& mapEntry : declarationIterator->second) {
            auto entryNode = eval(mapEntry.value);
            if (!entryNode.has_value()) {
                return std::nullopt;
            }

            if (mapEntry.key.isDefault) {
                defaultValue = *entryNode;
                continue;
            }

            if (matchesMapKey(mapEntry.key.token, selectorNode->value)) {
                return EvaluatedNode{
                    .value = entryNode->value,
                    .reason = selectorNode->reason + " mapped to " + valueToString(entryNode->value)};
            }
        }

        if (defaultValue.has_value()) {
            return EvaluatedNode{
                .value = defaultValue->value,
                .reason = selectorNode->reason + " mapped to " + valueToString(defaultValue->value)};
        }

        errors_.emplace_back("map call has no matching key and no default: " + mapCallNode.name);
        return std::nullopt;
    }

    [[nodiscard]] static bool matchesMapKey(const std::string& keyToken, const RuntimeValue& selectorValue) {
        double keyNumber = 0.0;
        if (parseDouble(keyToken, &keyNumber) && std::holds_alternative<double>(selectorValue)) {
            return std::fabs(std::get<double>(selectorValue) - keyNumber) < k_numeric_epsilon;
        }

        std::string normalizedKey = keyToken;
        if (normalizedKey.size() >= 2U && (normalizedKey.front() == '\'' || normalizedKey.front() == '"')
            && normalizedKey.back() == normalizedKey.front()) {
            normalizedKey = normalizedKey.substr(1U, normalizedKey.size() - 2U);
        }

        return normalizedKey == valueToString(selectorValue);
    }

    [[nodiscard]] std::optional<EvaluatedNode> compareRel(
        const EvaluatedNode& leftNode,
        const EvaluatedNode& rightNode,
        const std::string& relationOperator) {
        auto makeCompareNode = [&](const bool compareValue, const std::string& operatorText) {
            return EvaluatedNode{
                .value = RuntimeValue{compareValue},
                .reason = leftNode.reason + " is " + operatorText + " " + rightNode.reason};
        };

        const RuntimeValue& leftValue = leftNode.value;
        const RuntimeValue& rightValue = rightNode.value;
        const auto leftNumber = tryNumericLikeJs(leftValue);
        const auto rightNumber = tryNumericLikeJs(rightValue);
        if (leftNumber.has_value() && rightNumber.has_value()) {
            return makeCompareNode(
            evaluateRelation(*leftNumber, *rightNumber, relationOperator),
                relationOperatorSymbol(relationOperator));
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        const auto rightTime = tryTimeOfDay(rightValue);
        if (leftTime.has_value() && rightTime.has_value()) {
            return makeCompareNode(
                evaluateRelation(*leftTime, *rightTime, relationOperator),
                relationOperatorSymbol(relationOperator));
        }

        const std::string leftUndefinedVariable = extractUndefinedVariableName(leftNode.reason);
        const std::string rightUndefinedVariable = extractUndefinedVariableName(rightNode.reason);

        std::ostringstream errorText;
        const std::string operatorText = relationOperatorSymbol(relationOperator);

        errorText << "relational comparison failed: "
                  << leftNode.reason << " " << operatorText << " " << rightNode.reason
                  << " cannot be evaluated";
        appendUndefinedCause(&errorText, leftUndefinedVariable, rightUndefinedVariable);

        errorText << "; operands: operand1(type=" << valueTypeToString(leftValue)
                  << ", value=" << valueToString(leftValue)
                  << ", source=" << leftNode.reason << "), "
                  << "operand2(type=" << valueTypeToString(rightValue)
                  << ", value=" << valueToString(rightValue)
                  << ", source=" << rightNode.reason << ")";
        errors_.emplace_back(errorText.str());
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EvaluatedNode> evalAddSub(
        const EvaluatedNode& leftNode,
        const EvaluatedNode& rightNode,
        const bool isAddOperation) {
        const RuntimeValue& leftValue = leftNode.value;
        const RuntimeValue& rightValue = rightNode.value;
        const std::string operatorText = isAddOperation ? "+" : "-";
        if (std::holds_alternative<double>(leftValue) && std::holds_alternative<double>(rightValue)) {
            const double leftNumber = std::get<double>(leftValue);
            const double rightNumber = std::get<double>(rightValue);
            const RuntimeValue resultValue{isAddOperation ? leftNumber + rightNumber : leftNumber - rightNumber};
            return EvaluatedNode{
                .value = resultValue,
                .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
        }

        const auto leftTime = tryTimeOfDay(leftValue);
        if (leftTime.has_value() && std::holds_alternative<double>(rightValue)) {
            const auto deltaMinutes = std::llround(std::get<double>(rightValue));
            const auto deltaDuration = std::chrono::minutes{deltaMinutes};
            const auto resultTime = isAddOperation ? *leftTime + deltaDuration : *leftTime - deltaDuration;
            if (std::holds_alternative<std::chrono::system_clock::time_point>(leftValue)) {
                const auto dayStart = localDayStart(std::get<std::chrono::system_clock::time_point>(leftValue));
                if (!dayStart.has_value()) {
                    errors_.emplace_back("failed to derive local day start");
                    return std::nullopt;
                }
                const RuntimeValue resultValue{*dayStart + resultTime};
                return EvaluatedNode{
                    .value = resultValue,
                    .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
            }
            const RuntimeValue resultValue{formatTimeText(resultTime)};
            return EvaluatedNode{
                .value = resultValue,
                .reason = leftNode.reason + " " + operatorText + " " + rightNode.reason + " = " + valueToString(resultValue)};
        }

        errors_.emplace_back("invalid operands for arithmetic operation");
        return std::nullopt;
    }

    const FieldScriptAst& script_;
    const ExternalVariableMap& variables_;
    std::map<std::string, std::vector<MapEntryAst>> declarations_;
    std::set<std::string> usedVariables_;
    std::set<std::string> missingVariables_;
    std::vector<std::string> errors_;
};

} // namespace

ExpressionEvaluationResult ExpressionEvaluator::evaluate(
    const FieldScriptAst& script,
    const VariableMap& variables) {
    RuntimeEvaluator evaluator{script, variables};
    return evaluator.run();
}

} // namespace yaha
