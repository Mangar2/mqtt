#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "json/json_value.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yaha::http_mqtt_ops_internal {

std::string trimCopy(const std::string_view valueText) {
    std::size_t firstIndex = 0U;
    while (firstIndex < valueText.size() && std::isspace(static_cast<unsigned char>(valueText[firstIndex])) != 0) {
        ++firstIndex;
    }

    std::size_t lastIndex = valueText.size();
    while (lastIndex > firstIndex && std::isspace(static_cast<unsigned char>(valueText[lastIndex - 1U])) != 0) {
        --lastIndex;
    }

    return std::string{valueText.substr(firstIndex, lastIndex - firstIndex)};
}

std::string toLowerCopy(const std::string_view valueText) {
    std::string loweredText{};
    loweredText.reserve(valueText.size());
    for (const char currentChar : valueText) {
        loweredText.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(currentChar))));
    }
    return loweredText;
}

std::string toUpperCopy(const std::string_view valueText) {
    std::string upperText{};
    upperText.reserve(valueText.size());
    for (const char currentChar : valueText) {
        upperText.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(currentChar))));
    }
    return upperText;
}

std::string decodeTopicSlashEscapes(const std::string_view topicInput) {
    std::string normalizedTopic{};
    normalizedTopic.reserve(topicInput.size());

    std::size_t cursorIndex = 0U;
    while (cursorIndex < topicInput.size()) {
        if (cursorIndex + 2U < topicInput.size() &&
            topicInput[cursorIndex] == '%' &&
            topicInput[cursorIndex + 1U] == '2' &&
            (topicInput[cursorIndex + 2U] == 'F' || topicInput[cursorIndex + 2U] == 'f')) {
            normalizedTopic.push_back('/');
            cursorIndex += 3U;
            continue;
        }

        normalizedTopic.push_back(topicInput[cursorIndex]);
        ++cursorIndex;
    }

    return normalizedTopic;
}

std::string escapeJsonString(const std::string_view valueText) {
    std::string escapedText{};
    escapedText.reserve(valueText.size() + k_escapeReservePadding);
    for (const char currentChar : valueText) {
        switch (currentChar) {
            case '"':
                escapedText += "\\\"";
                break;
            case '\\':
                escapedText += "\\\\";
                break;
            case '\n':
                escapedText += "\\n";
                break;
            case '\r':
                escapedText += "\\r";
                break;
            case '\t':
                escapedText += "\\t";
                break;
            default:
                escapedText.push_back(currentChar);
                break;
        }
    }

    return escapedText;
}

std::string messageValueToJson(const Value& valueInput) {
    if (std::holds_alternative<std::string>(valueInput)) {
        return std::format("\"{}\"", escapeJsonString(std::get<std::string>(valueInput)));
    }

    std::ostringstream outputStream{};
    outputStream << std::get<double>(valueInput);
    return outputStream.str();
}

std::string reasonToJson(const Message& messageInput) {
    std::ostringstream outputStream{};
    outputStream << '[';
    bool firstEntry = true;
    for (const auto& reasonEntry : messageInput.reason()) {
        if (!firstEntry) {
            outputStream << ',';
        }
        firstEntry = false;
        outputStream << std::format(
            R"({{"message":"{}","timestamp":"{}"}})",
            escapeJsonString(reasonEntry.message),
            escapeJsonString(reasonEntry.timestamp));
    }
    outputStream << ']';

    return outputStream.str();
}

namespace {
[[nodiscard]] std::optional<mqtt::json::JsonValue> tryParseObjectValue(const std::string_view jsonText) {
    auto parsedValue = mqtt::json::JsonValue::try_parse(trimCopy(jsonText));
    if (!parsedValue.has_value() || !parsedValue->is_object()) {
        return std::nullopt;
    }
    return parsedValue;
}

} // namespace

std::optional<std::string> tryExtractStringField(
    const std::string_view objectText,
    const std::string_view keyName) {
    const auto parsedObject = tryParseObjectValue(objectText);
    if (!parsedObject.has_value() || !parsedObject->contains(keyName)) {
        return std::nullopt;
    }

    const auto& valueField = parsedObject->at(keyName);
    if (!valueField.is_string()) {
        return std::nullopt;
    }
    return valueField.as_string();
}

std::optional<int> tryExtractIntegerField(
    const std::string_view objectText,
    const std::string_view keyName) {
    const auto parsedObject = tryParseObjectValue(objectText);
    if (!parsedObject.has_value() || !parsedObject->contains(keyName)) {
        return std::nullopt;
    }

    const auto& valueField = parsedObject->at(keyName);
    if (!valueField.is_number()) {
        return std::nullopt;
    }

    const double numericValue = valueField.as_number();
    if (!std::isfinite(numericValue) || std::floor(numericValue) != numericValue ||
        numericValue < static_cast<double>(std::numeric_limits<int>::min()) ||
        numericValue > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    return static_cast<int>(numericValue);
}

std::optional<std::string> extractRawToken(
    const std::string_view objectText,
    const std::string_view keyName) {
    const auto parsedObject = tryParseObjectValue(objectText);
    if (!parsedObject.has_value() || !parsedObject->contains(keyName)) {
        return std::nullopt;
    }

    const std::string tokenText = parsedObject->at(keyName).stringify();
    return trimCopy(tokenText);
}

std::optional<Value> parseJsonValueToken(const std::string_view tokenInput) {
    const auto parsedToken = mqtt::json::JsonValue::try_parse(trimCopy(tokenInput));
    if (!parsedToken.has_value()) {
        return std::nullopt;
    }

    if (parsedToken->is_string()) {
        return Value{parsedToken->as_string()};
    }

    if (parsedToken->is_number()) {
        return Value{parsedToken->as_number()};
    }

    if (parsedToken->is_boolean()) {
        return Value{parsedToken->as_boolean() ? std::string{"true"} : std::string{"false"}};
    }

    if (parsedToken->is_null()) {
        return Value{std::string{"null"}};
    }

    return std::nullopt;
}

std::optional<ReasonList> parseReasonArray(const std::string_view arrayText) {
    const auto parsedArray = mqtt::json::JsonValue::try_parse(trimCopy(arrayText));
    if (!parsedArray.has_value() || !parsedArray->is_array()) {
        return std::nullopt;
    }

    ReasonList entriesOutput{};
    for (const auto& reasonValue : parsedArray->as_array()) {
        if (!reasonValue.is_object()) {
            return std::nullopt;
        }

        if (!reasonValue.contains("message") || !reasonValue.at("message").is_string()) {
            return std::nullopt;
        }

        std::string timestampText{};
        if (reasonValue.contains("timestamp") && reasonValue.at("timestamp").is_string()) {
            timestampText = reasonValue.at("timestamp").as_string();
        }

        entriesOutput.push_back(ReasonEntry{
            .message = reasonValue.at("message").as_string(),
            .timestamp = std::move(timestampText),
        });
    }

    return entriesOutput;
}

void appendReasonsPreservingOrder(
    Message& messageOutput,
    const ReasonList& reasonEntries) {
    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        const ReasonEntry& entry = reasonEntries[reverseIndex - 1U];
        messageOutput.addReason(entry.message, entry.timestamp);
    }
}

std::optional<Qos> parseQosField(const std::string_view textInput) {
    const std::string trimmedText = trimCopy(textInput);
    if (trimmedText.empty()) {
        return std::nullopt;
    }

    int parsedQos = 0;
    const auto parseResult = std::from_chars(trimmedText.data(), trimmedText.data() + trimmedText.size(), parsedQos);
    if (parseResult.ec != std::errc{} || parseResult.ptr != trimmedText.data() + trimmedText.size()) {
        return std::nullopt;
    }

    if (parsedQos < 0 || parsedQos > 2) {
        return std::nullopt;
    }

    return static_cast<Qos>(parsedQos);
}

std::optional<bool> parseRetainField(const std::string_view textInput) {
    const std::string loweredText = toLowerCopy(trimCopy(textInput));
    if (loweredText.empty()) {
        return std::nullopt;
    }
    if (loweredText == "1" || loweredText == "true") {
        return true;
    }
    if (loweredText == "0" || loweredText == "false") {
        return false;
    }
    return std::nullopt;
}

std::string serializeTopics(const HttpMqttTopics& topicsInput) {
    std::ostringstream outputStream{};
    outputStream << '{';

    bool firstEntry = true;
    for (const auto& [topicFilter, qosValue] : topicsInput) {
        if (!firstEntry) {
            outputStream << ',';
        }
        firstEntry = false;
        outputStream << std::format("\"{}\":{}", escapeJsonString(topicFilter), static_cast<int>(qosValue));
    }

    outputStream << '}';
    return outputStream.str();
}

std::string serializeUInt8Array(const std::vector<std::uint8_t>& valuesInput) {
    std::ostringstream outputStream{};
    outputStream << '[';

    bool firstEntry = true;
    for (const std::uint8_t valueItem : valuesInput) {
        if (!firstEntry) {
            outputStream << ',';
        }
        firstEntry = false;
        outputStream << static_cast<unsigned int>(valueItem);
    }

    outputStream << ']';
    return outputStream.str();
}

std::vector<int> parseIntegerArrayPayload(const std::string_view payloadText) {
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(trimCopy(payloadText));
    if (!parsedRoot.has_value()) {
        throw std::runtime_error{"result payload must be a JSON array"};
    }

    const mqtt::json::JsonValue::Array* valuesArray = nullptr;
    if (parsedRoot->is_array()) {
        valuesArray = &parsedRoot->as_array();
    } else if (parsedRoot->is_object() && parsedRoot->contains("qos") && parsedRoot->at("qos").is_array()) {
        valuesArray = &parsedRoot->at("qos").as_array();
    }

    if (valuesArray == nullptr) {
        throw std::runtime_error{"result payload must be a JSON array"};
    }

    std::vector<int> valuesOutput{};
    for (const auto& valueItem : *valuesArray) {
        if (!valueItem.is_number()) {
            throw std::runtime_error{"result payload contains non-integer array value"};
        }

        const double numericValue = valueItem.as_number();
        if (!std::isfinite(numericValue) || std::floor(numericValue) != numericValue ||
            numericValue < static_cast<double>(std::numeric_limits<int>::min()) ||
            numericValue > static_cast<double>(std::numeric_limits<int>::max())) {
            throw std::runtime_error{"result payload contains non-integer array value"};
        }

        valuesOutput.push_back(static_cast<int>(numericValue));
    }

    return valuesOutput;
}

void validateStatusCode(const HttpMqttResult& resultInput, const int expectedStatus, const std::string_view contextText) {
    if (resultInput.statusCode != expectedStatus) {
        throw std::runtime_error{
            std::format("{}: invalid status {} expected {}", contextText, resultInput.statusCode, expectedStatus)};
    }
}

void validateContentTypeJson(const HttpMqttResult& resultInput, const std::string_view contextText) {
    if (!headerValueStartsWith(resultInput.headers, "content-type", k_contentTypeJsonPrefix)) {
        throw std::runtime_error{std::format("{}: invalid content-type header", contextText)};
    }
}

void validateHeaderEquals(
    const HttpMqttResult& resultInput,
    const std::string_view headerName,
    const std::string_view expectedValue,
    const std::string_view contextText) {
    const std::string actualValue = requireHeaderValue(resultInput.headers, headerName);
    if (actualValue != expectedValue) {
        throw std::runtime_error{std::format(
            "{}: invalid header '{}' value '{}' expected '{}'",
            contextText,
            headerName,
            actualValue,
            expectedValue)};
    }
}

void validatePacketIdMatch(
    const HttpMqttResult& resultInput,
    const std::optional<std::uint16_t> expectedPacketId,
    const std::string_view contextText) {
    if (!expectedPacketId.has_value()) {
        return;
    }

    const auto receivedPacketId = readPacketIdHeader(resultInput.headers);
    if (!receivedPacketId.has_value() || receivedPacketId != expectedPacketId) {
        throw std::runtime_error{std::format("{}: packetid mismatch", contextText)};
    }
}

} // namespace yaha::http_mqtt_ops_internal
