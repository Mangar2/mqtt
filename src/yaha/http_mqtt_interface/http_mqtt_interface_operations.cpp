#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

namespace {

constexpr std::string_view k_versionValue{"1.0"};
constexpr std::string_view k_contentTypeJsonPrefix{"application/json"};
constexpr int k_httpStatusOk{200};
constexpr int k_httpStatusBadRequest{400};
constexpr int k_httpStatusMethodNotAllowed{405};
constexpr int k_httpStatusNoContent{204};
constexpr int k_httpStatusInternalServerError{500};
constexpr int k_connectCodeMax{5};
constexpr int k_subscribeCodeFailLegacy{127};
constexpr int k_subscribeCodeFailModern{128};
constexpr int k_unsubscribeNoSubscription{17};
constexpr std::size_t k_escapeReservePadding{8U};
constexpr std::string_view k_browserReasonMessage{"Request by browser"};

[[nodiscard]] std::string trimCopy(const std::string_view valueText) {
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

[[nodiscard]] std::string toLowerCopy(const std::string_view valueText) {
    std::string loweredText{};
    loweredText.reserve(valueText.size());
    for (const char currentChar : valueText) {
        loweredText.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(currentChar))));
    }
    return loweredText;
}

[[nodiscard]] std::string toUpperCopy(const std::string_view valueText) {
    std::string upperText{};
    upperText.reserve(valueText.size());
    for (const char currentChar : valueText) {
        upperText.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(currentChar))));
    }
    return upperText;
}

[[nodiscard]] std::string decodeTopicSlashEscapes(const std::string_view topicInput) {
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

[[nodiscard]] HttpMqttResult makeCompatibilityErrorResponse(
    const int statusCode,
    const std::string_view errorCode) {
    HttpMqttResult responseOutput{};
    responseOutput.statusCode = statusCode;
    responseOutput.headers = makeStandardJsonHeaders();
    responseOutput.headers["version"] = std::string{k_versionValue};
    responseOutput.payload = std::format("{{\"error\":\"{}\"}}", errorCode);
    return responseOutput;
}

[[nodiscard]] bool isSupportedCompatibilityRoute(
    const std::string_view methodInput,
    const std::string_view endpointInput,
    const HttpMqttPublishCompatibilityConfig& configInput) {
    const std::string upperMethod = toUpperCopy(methodInput);
    if (upperMethod == "PUT" && endpointInput == "/publish") {
        return true;
    }

    if (upperMethod == "POST" && endpointInput == "/publish") {
        return true;
    }

    if (upperMethod == "POST" && endpointInput == "/publish.php") {
        return configInput.enablePublishPhpAlias;
    }

    return false;
}

[[nodiscard]] std::string escapeJsonString(const std::string_view valueText) {
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

[[nodiscard]] std::string messageValueToJson(const Value& valueInput) {
    if (std::holds_alternative<std::string>(valueInput)) {
        return std::format("\"{}\"", escapeJsonString(std::get<std::string>(valueInput)));
    }

    std::ostringstream outputStream{};
    outputStream << std::get<double>(valueInput);
    return outputStream.str();
}

[[nodiscard]] std::string reasonToJson(const Message& messageInput) {
    std::ostringstream outputStream{};
    outputStream << '[';
    bool firstEntry = true;
    for (const auto& reasonEntry : messageInput.reason()) {
        if (!firstEntry) {
            outputStream << ',';
        }
        firstEntry = false;
        outputStream << std::format(
            "{{\"message\":\"{}\",\"timestamp\":\"{}\"}}",
            escapeJsonString(reasonEntry.message),
            escapeJsonString(reasonEntry.timestamp));
    }
    outputStream << ']';

    return outputStream.str();
}

[[nodiscard]] bool tryFindObjectRange(
    const std::string_view textValue,
    const std::string_view keyName,
    std::size_t& objectStart,
    std::size_t& objectEnd) {
    const std::string keyToken = std::format("\"{}\"", keyName);
    const std::size_t keyPosition = textValue.find(keyToken);
    if (keyPosition == std::string::npos) {
        return false;
    }

    std::size_t cursorIndex = textValue.find(':', keyPosition + keyToken.size());
    if (cursorIndex == std::string::npos) {
        return false;
    }
    ++cursorIndex;

    while (cursorIndex < textValue.size() && std::isspace(static_cast<unsigned char>(textValue[cursorIndex])) != 0) {
        ++cursorIndex;
    }
    if (cursorIndex >= textValue.size() || textValue[cursorIndex] != '{') {
        return false;
    }

    int depthValue = 0;
    for (std::size_t scanIndex = cursorIndex; scanIndex < textValue.size(); ++scanIndex) {
        if (textValue[scanIndex] == '{') {
            ++depthValue;
        } else if (textValue[scanIndex] == '}') {
            --depthValue;
            if (depthValue == 0) {
                objectStart = cursorIndex;
                objectEnd = scanIndex;
                return true;
            }
        }
    }

    return false;
}

[[nodiscard]] std::optional<std::string> tryExtractStringField(
    const std::string_view objectText,
    const std::string_view keyName) {
    const std::string keyToken = std::format("\"{}\"", keyName);
    const std::size_t keyPosition = objectText.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorIndex = objectText.find(':', keyPosition + keyToken.size());
    if (cursorIndex == std::string::npos) {
        return std::nullopt;
    }
    ++cursorIndex;

    while (cursorIndex < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[cursorIndex])) != 0) {
        ++cursorIndex;
    }
    if (cursorIndex >= objectText.size() || objectText[cursorIndex] != '"') {
        return std::nullopt;
    }

    ++cursorIndex;
    std::string parsedText{};
    while (cursorIndex < objectText.size()) {
        const char currentChar = objectText[cursorIndex];
        if (currentChar == '\\') {
            ++cursorIndex;
            if (cursorIndex >= objectText.size()) {
                return std::nullopt;
            }
            parsedText.push_back(objectText[cursorIndex]);
            ++cursorIndex;
            continue;
        }
        if (currentChar == '"') {
            return parsedText;
        }
        parsedText.push_back(currentChar);
        ++cursorIndex;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<int> tryExtractIntegerField(
    const std::string_view objectText,
    const std::string_view keyName) {
    const std::string keyToken = std::format("\"{}\"", keyName);
    const std::size_t keyPosition = objectText.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorIndex = objectText.find(':', keyPosition + keyToken.size());
    if (cursorIndex == std::string::npos) {
        return std::nullopt;
    }
    ++cursorIndex;

    while (cursorIndex < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[cursorIndex])) != 0) {
        ++cursorIndex;
    }
    if (cursorIndex >= objectText.size()) {
        return std::nullopt;
    }

    std::size_t endIndex = cursorIndex;
    while (endIndex < objectText.size() &&
           (std::isdigit(static_cast<unsigned char>(objectText[endIndex])) != 0 || objectText[endIndex] == '-')) {
        ++endIndex;
    }

    if (endIndex <= cursorIndex) {
        return std::nullopt;
    }

    int parsedValue = 0;
    const auto* beginPtr = objectText.data() + static_cast<std::ptrdiff_t>(cursorIndex);
    const auto* endPtr = objectText.data() + static_cast<std::ptrdiff_t>(endIndex);
    const auto parseResult = std::from_chars(beginPtr, endPtr, parsedValue);
    if (parseResult.ec != std::errc{} || parseResult.ptr != endPtr) {
        return std::nullopt;
    }

    return parsedValue;
}

struct JsonTokenScanState {
    int objectDepth{0};
    int arrayDepth{0};
    bool inString{false};
    bool escaped{false};
};

[[nodiscard]] bool advanceScanState(const char currentChar, JsonTokenScanState& stateOutput) {
    if (stateOutput.inString) {
        if (stateOutput.escaped) {
            stateOutput.escaped = false;
        } else if (currentChar == '\\') {
            stateOutput.escaped = true;
        } else if (currentChar == '"') {
            stateOutput.inString = false;
        }
        return true;
    }

    if (currentChar == '"') {
        stateOutput.inString = true;
        return true;
    }
    if (currentChar == '{') {
        ++stateOutput.objectDepth;
        return true;
    }
    if (currentChar == '}') {
        --stateOutput.objectDepth;
        return stateOutput.objectDepth >= -1;
    }
    if (currentChar == '[') {
        ++stateOutput.arrayDepth;
        return true;
    }
    if (currentChar == ']') {
        --stateOutput.arrayDepth;
        return stateOutput.arrayDepth >= 0;
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> tryFindRawTokenEnd(
    const std::string_view objectText,
    const std::size_t tokenStart) {
    JsonTokenScanState state{};
    for (std::size_t cursorIndex = tokenStart; cursorIndex < objectText.size(); ++cursorIndex) {
        const char currentChar = objectText[cursorIndex];
        if (!advanceScanState(currentChar, state)) {
            return std::nullopt;
        }
        if (!state.inString && state.objectDepth == 0 && state.arrayDepth == 0 && currentChar == ',') {
            return cursorIndex;
        }
        if (!state.inString && state.objectDepth == -1 && state.arrayDepth == 0 && currentChar == '}') {
            return cursorIndex;
        }
    }

    if (state.inString || state.objectDepth != 0 || state.arrayDepth != 0) {
        return std::nullopt;
    }
    return objectText.size();
}

[[nodiscard]] std::optional<std::string> extractRawToken(
    const std::string_view objectText,
    const std::string_view keyName) {
    const std::string keyToken = std::format("\"{}\"", keyName);
    const std::size_t keyPosition = objectText.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t tokenStart = objectText.find(':', keyPosition + keyToken.size());
    if (tokenStart == std::string::npos) {
        return std::nullopt;
    }
    ++tokenStart;

    while (tokenStart < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[tokenStart])) != 0) {
        ++tokenStart;
    }
    if (tokenStart >= objectText.size()) {
        return std::nullopt;
    }

    const std::optional<std::size_t> tokenEnd = tryFindRawTokenEnd(objectText, tokenStart);
    if (!tokenEnd.has_value() || *tokenEnd < tokenStart) {
        return std::nullopt;
    }

    std::string token = trimCopy(objectText.substr(tokenStart, *tokenEnd - tokenStart));
    if (token.empty()) {
        return std::nullopt;
    }
    return token;
}

[[nodiscard]] std::optional<Value> parseJsonValueToken(const std::string_view tokenInput) {
    const std::string tokenText = trimCopy(tokenInput);
    if (tokenText.empty()) {
        return std::nullopt;
    }

    if (tokenText.front() == '"' && tokenText.back() == '"') {
        const std::string wrappedValue = std::format("{{\"value\":{}}}", tokenText);
        const std::optional<std::string> parsedString = tryExtractStringField(wrappedValue, "value");
        if (!parsedString.has_value()) {
            return std::nullopt;
        }
        return Value{*parsedString};
    }

    const std::string lowered = toLowerCopy(tokenText);
    if (lowered == "true" || lowered == "false" || lowered == "null") {
        return Value{lowered};
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(tokenText.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return std::nullopt;
    }

    return Value{parsedNumber};
}

[[nodiscard]] bool isReasonArrayShapeValid(const std::string& textInput) {
    return !textInput.empty() && textInput.front() == '[' && textInput.back() == ']';
}

[[nodiscard]] std::optional<std::size_t> tryFindReasonObjectEnd(
    const std::string_view reasonArray,
    const std::size_t objectStart,
    const std::size_t endIndex) {
    JsonTokenScanState state{};
    for (std::size_t cursorIndex = objectStart; cursorIndex < endIndex; ++cursorIndex) {
        const char currentChar = reasonArray[cursorIndex];
        if (!advanceScanState(currentChar, state)) {
            return std::nullopt;
        }
        if (!state.inString && state.objectDepth == 0 && currentChar == '}') {
            return cursorIndex;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ReasonEntry> tryParseReasonEntry(const std::string_view objectText) {
    const std::optional<std::string> messageText = tryExtractStringField(objectText, "message");
    const std::optional<std::string> timestampText = tryExtractStringField(objectText, "timestamp");
    if (!messageText.has_value() || !timestampText.has_value()) {
        return std::nullopt;
    }
    return ReasonEntry{*messageText, *timestampText};
}

[[nodiscard]] std::optional<std::vector<ReasonEntry>> parseReasonArray(const std::string_view arrayText) {
    const std::string trimmedArray = trimCopy(arrayText);
    if (!isReasonArrayShapeValid(trimmedArray)) {
        return std::nullopt;
    }

    std::vector<ReasonEntry> entriesOutput{};
    std::size_t cursorIndex = 1U;
    const std::size_t endIndex = trimmedArray.size() - 1U;

    while (cursorIndex < endIndex) {
        while (cursorIndex < endIndex && std::isspace(static_cast<unsigned char>(trimmedArray[cursorIndex])) != 0) {
            ++cursorIndex;
        }
        if (cursorIndex >= endIndex) {
            break;
        }

        if (trimmedArray[cursorIndex] != '{') {
            return std::nullopt;
        }

        const std::optional<std::size_t> objectEnd = tryFindReasonObjectEnd(trimmedArray, cursorIndex, endIndex);
        if (!objectEnd.has_value() || *objectEnd < cursorIndex) {
            return std::nullopt;
        }

        const std::string_view objectText =
            std::string_view{trimmedArray}.substr(cursorIndex, *objectEnd - cursorIndex + 1U);
        const std::optional<ReasonEntry> parsedEntry = tryParseReasonEntry(objectText);
        if (!parsedEntry.has_value()) {
            return std::nullopt;
        }
        entriesOutput.push_back(*parsedEntry);

        cursorIndex = *objectEnd + 1U;

        while (cursorIndex < endIndex && std::isspace(static_cast<unsigned char>(trimmedArray[cursorIndex])) != 0) {
            ++cursorIndex;
        }
        if (cursorIndex < endIndex && trimmedArray[cursorIndex] == ',') {
            ++cursorIndex;
            continue;
        }
        if (cursorIndex < endIndex) {
            return std::nullopt;
        }
    }

    return entriesOutput;
}

void appendReasonsPreservingOrder(
    Message& messageOutput,
    const std::vector<ReasonEntry>& reasonEntries) {
    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        const ReasonEntry& entry = reasonEntries[reverseIndex - 1U];
        messageOutput.addReason(entry.message, entry.timestamp);
    }
}

[[nodiscard]] std::optional<Qos> parseQosField(const std::string_view textInput) {
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

[[nodiscard]] std::optional<bool> parseRetainField(const std::string_view textInput) {
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

struct CompatibilityBodyFields {
    std::optional<std::string> topic{};
    std::optional<Value> value{};
    std::optional<std::vector<ReasonEntry>> reason{};
    std::optional<Qos> qos{};
    std::optional<bool> retain{};
};

[[nodiscard]] std::optional<CompatibilityBodyFields> tryParseCompatibilityBody(
    const std::string_view bodyText) {
    try {
        requireJsonObjectPayload(bodyText, "publish compatibility body");
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }

    CompatibilityBodyFields fieldsOutput{};

    fieldsOutput.topic = tryExtractStringField(bodyText, "topic");

    const std::optional<std::string> valueToken = extractRawToken(bodyText, "value");
    if (valueToken.has_value()) {
        const std::optional<Value> parsedValue = parseJsonValueToken(*valueToken);
        if (!parsedValue.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.value = parsedValue;
    }

    const std::optional<std::string> reasonToken = extractRawToken(bodyText, "reason");
    if (reasonToken.has_value()) {
        const std::optional<std::vector<ReasonEntry>> parsedReason = parseReasonArray(*reasonToken);
        if (!parsedReason.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.reason = parsedReason;
    }

    const std::optional<std::string> qosToken = extractRawToken(bodyText, "qos");
    if (qosToken.has_value()) {
        const std::optional<Qos> parsedQos = parseQosField(*qosToken);
        if (!parsedQos.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.qos = parsedQos;
    }

    const std::optional<std::string> retainToken = extractRawToken(bodyText, "retain");
    if (retainToken.has_value()) {
        const std::optional<bool> parsedRetain = parseRetainField(*retainToken);
        if (!parsedRetain.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.retain = parsedRetain;
    }

    return fieldsOutput;
}

struct CompatibilityParsedFields {
    std::string topic{};
    std::optional<Value> value{};
    std::optional<std::vector<ReasonEntry>> reason{};
    std::optional<Qos> qos{};
    std::optional<bool> retain{};
};

[[nodiscard]] std::optional<CompatibilityParsedFields> tryParseCompatibilityFields(
    const HttpMqttHeaders& normalizedFields) {
    CompatibilityParsedFields fieldsOutput{};

    fieldsOutput.topic = trimCopy(tryReadHeaderValue(normalizedFields, "topic").value_or(""));

    if (const auto extractedValue = tryReadHeaderValue(normalizedFields, "value"); extractedValue.has_value()) {
        fieldsOutput.value = Value{*extractedValue};
    }

    if (const auto reasonField = tryReadHeaderValue(normalizedFields, "reason"); reasonField.has_value()) {
        const std::optional<std::vector<ReasonEntry>> parsedReason = parseReasonArray(*reasonField);
        if (!parsedReason.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.reason = parsedReason;
    }

    if (const auto qosField = tryReadHeaderValue(normalizedFields, "qos"); qosField.has_value()) {
        fieldsOutput.qos = parseQosField(*qosField);
    }

    if (const auto retainField = tryReadHeaderValue(normalizedFields, "retain"); retainField.has_value()) {
        fieldsOutput.retain = parseRetainField(*retainField);
    }

    return fieldsOutput;
}

[[nodiscard]] bool applyCompatibilityBodyFallback(
    CompatibilityParsedFields& parsedFields,
    const std::string_view bodyText) {
    if (!parsedFields.topic.empty()) {
        return true;
    }

    const std::string trimmedBody = trimCopy(bodyText);
    if (trimmedBody.empty()) {
        return true;
    }

    const std::optional<CompatibilityBodyFields> bodyFields = tryParseCompatibilityBody(trimmedBody);
    if (!bodyFields.has_value()) {
        return false;
    }

    if (bodyFields->topic.has_value()) {
        parsedFields.topic = trimCopy(*bodyFields->topic);
    }
    if (!parsedFields.value.has_value() && bodyFields->value.has_value()) {
        parsedFields.value = bodyFields->value;
    }
    if (!parsedFields.reason.has_value() && bodyFields->reason.has_value()) {
        parsedFields.reason = bodyFields->reason;
    }
    if (!parsedFields.qos.has_value() && bodyFields->qos.has_value()) {
        parsedFields.qos = bodyFields->qos;
    }
    if (!parsedFields.retain.has_value() && bodyFields->retain.has_value()) {
        parsedFields.retain = bodyFields->retain;
    }

    return true;
}

[[nodiscard]] std::optional<HttpMqttResult> validateCompatibilityTopic(CompatibilityParsedFields& parsedFields) {
    parsedFields.topic = decodeTopicSlashEscapes(parsedFields.topic);
    if (parsedFields.topic.empty()) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "missing_topic");
    }
    return std::nullopt;
}

[[nodiscard]] Message buildCompatibilityMessage(const CompatibilityParsedFields& parsedFields) {
    Message mappedMessage{
        parsedFields.topic,
        parsedFields.value.value_or(Value{std::string{}}),
        parsedFields.qos.value_or(Qos::AtLeastOnce),
        parsedFields.retain.value_or(false)};

    if (parsedFields.reason.has_value() && !parsedFields.reason->empty()) {
        appendReasonsPreservingOrder(mappedMessage, *parsedFields.reason);
    } else {
        mappedMessage.addReason(std::string{k_browserReasonMessage});
    }

    return mappedMessage;
}

[[nodiscard]] HttpMqttResult tryForwardCompatibilityPublish(
    const HttpMqttPublishCompatibilityForwarder& forwarder,
    const HttpMqttRequestData& mappedRequest,
    const Message& mappedMessage) {
    HttpMqttResult downstreamResult = forwarder(mappedRequest, mappedMessage);

    if (downstreamResult.statusCode != k_httpStatusNoContent) {
        return makeCompatibilityErrorResponse(k_httpStatusInternalServerError, "internal_failure");
    }
    return downstreamResult;
}

[[nodiscard]] HttpMqttResult adaptCompatibilityResponse(
    const HttpMqttResult& downstreamResult,
    const HttpMqttPublishCompatibilityConfig& configInput) {
    if (configInput.responseMode == HttpMqttPublishCompatibilityResponseMode::Native) {
        return downstreamResult;
    }

    HttpMqttResult compatibilityResult{};
    compatibilityResult.statusCode = k_httpStatusOk;
    compatibilityResult.headers = makeStandardJsonHeaders();
    compatibilityResult.headers["version"] = std::string{k_versionValue};
    compatibilityResult.payload = std::format("\"{}\"", escapeJsonString(downstreamResult.payload));
    return compatibilityResult;
}

[[nodiscard]] std::string serializeTopics(const HttpMqttTopics& topicsInput) {
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

[[nodiscard]] std::string serializeUInt8Array(const std::vector<std::uint8_t>& valuesInput) {
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

[[nodiscard]] std::vector<int> parseIntegerArrayPayload(const std::string_view payloadText) {
    const std::string trimmedPayload = trimCopy(payloadText);
    if (trimmedPayload.empty()) {
        throw std::runtime_error{"result payload must be a JSON array"};
    }

    std::size_t startIndex = trimmedPayload.find('[');
    std::size_t endIndex = trimmedPayload.rfind(']');
    if (startIndex == std::string::npos || endIndex == std::string::npos || endIndex < startIndex) {
        throw std::runtime_error{"result payload must be a JSON array"};
    }

    std::vector<int> valuesOutput{};
    std::size_t cursorIndex = startIndex + 1U;
    while (cursorIndex < endIndex) {
        while (cursorIndex < endIndex && std::isspace(static_cast<unsigned char>(trimmedPayload[cursorIndex])) != 0) {
            ++cursorIndex;
        }

        if (cursorIndex >= endIndex) {
            break;
        }

        std::size_t tokenEndIndex = cursorIndex;
        while (tokenEndIndex < endIndex && trimmedPayload[tokenEndIndex] != ',') {
            ++tokenEndIndex;
        }

        const std::string tokenText = trimCopy(std::string_view{trimmedPayload}.substr(cursorIndex, tokenEndIndex - cursorIndex));
        if (!tokenText.empty()) {
            int parsedValue = 0;
            const auto parseResult = std::from_chars(tokenText.data(), tokenText.data() + tokenText.size(), parsedValue);
            if (parseResult.ec != std::errc{} || parseResult.ptr != tokenText.data() + tokenText.size()) {
                throw std::runtime_error{"result payload contains non-integer array value"};
            }
            valuesOutput.push_back(parsedValue);
        }

        cursorIndex = tokenEndIndex + 1U;
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

[[nodiscard]] std::string mqttCodeErrorMessage(const int mqttCode) {
    switch (mqttCode) {
        case 1:
            return "mqtt connect error: unacceptable protocol version";
        case 2:
            return "mqtt connect error: identifier rejected";
        case 3:
            return "mqtt connect error: server unavailable";
        case 4:
            return "mqtt connect error: bad username or password";
        case k_connectCodeMax:
            return "mqtt connect error: not authorized";
        default:
            return std::format("mqtt connect error: code {}", mqttCode);
    }
}

[[nodiscard]] HttpMqttRequestData buildConnectV1Request(const HttpMqttConnectOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};

    const std::uint32_t keepAliveValue = optionsInput.keepAlive.value_or(0U);
    std::ostringstream outputStream{};
    outputStream << '{';
    outputStream << std::format("\"clean\":{}", optionsInput.clean ? "true" : "false");
    outputStream << std::format(",\"keepAlive\":{}", static_cast<unsigned long long>(keepAliveValue));
    if (optionsInput.clientId.has_value()) {
        outputStream << std::format(",\"clientId\":\"{}\"", escapeJsonString(*optionsInput.clientId));
    }
    if (optionsInput.host.has_value()) {
        outputStream << std::format(",\"host\":\"{}\"", escapeJsonString(*optionsInput.host));
    }
    if (optionsInput.port.has_value()) {
        outputStream << std::format(",\"port\":{}", static_cast<unsigned int>(*optionsInput.port));
    }
    if (optionsInput.user.has_value()) {
        outputStream << std::format(",\"user\":\"{}\"", escapeJsonString(*optionsInput.user));
    }
    if (optionsInput.password.has_value()) {
        outputStream << std::format(",\"password\":\"{}\"", escapeJsonString(*optionsInput.password));
    }
    outputStream << '}';
    requestData.payload = outputStream.str();

    requestData.resultCheck = [](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusOk, "connect result");
        validateContentTypeJson(resultInput, "connect result");
        validateHeaderEquals(resultInput, "packet", "connack", "connect result");
        requireJsonObjectPayload(resultInput.payload, "connect result");

        const std::optional<int> presentValue = tryExtractIntegerField(resultInput.payload, "present");
        if (!presentValue.has_value() || (*presentValue != 0 && *presentValue != 1)) {
            throw std::runtime_error{"connect result: present must be 0 or 1"};
        }

        const std::optional<int> mqttCode = tryExtractIntegerField(resultInput.payload, "mqttcode");
        if (mqttCode.has_value()) {
            if (*mqttCode == 0) {
                return;
            }
            if (*mqttCode >= 1 && *mqttCode <= k_connectCodeMax) {
                throw std::runtime_error{mqttCodeErrorMessage(*mqttCode)};
            }
            throw std::runtime_error{"connect result: invalid mqttcode"};
        }

        std::size_t tokenStart = 0U;
        std::size_t tokenEnd = 0U;
        if (!tryFindObjectRange(resultInput.payload, "token", tokenStart, tokenEnd)) {
            throw std::runtime_error{"connect result: missing token object"};
        }

        const std::string tokenObject = resultInput.payload.substr(tokenStart, tokenEnd - tokenStart + 1U);
        const std::optional<std::string> sendToken = tryExtractStringField(tokenObject, "send");
        const std::optional<std::string> receiveToken = tryExtractStringField(tokenObject, "receive");
        if (!sendToken.has_value() || !receiveToken.has_value() ||
            sendToken->empty() || receiveToken->empty()) {
            throw std::runtime_error{"connect result: token.send and token.receive must be strings"};
        }
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildConnectV1Response(const HttpMqttConnectResult& resultInput) {
    if (resultInput.present != 0U && resultInput.present != 1U) {
        throw std::runtime_error{"onConnect: present must be 0 or 1"};
    }

    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["packet"] = "connack";
    resultOutput.headers["version"] = std::string{k_versionValue};

    std::ostringstream outputStream{};
    outputStream << '{';
    if (resultInput.mqttCode.has_value()) {
        outputStream << std::format(
            "\"mqttcode\":{},",
            static_cast<unsigned int>(*resultInput.mqttCode));
    }
    outputStream << std::format("\"present\":{}", static_cast<unsigned int>(resultInput.present));
    outputStream << std::format(
        ",\"token\":{{\"send\":\"{}\",\"receive\":\"{}\"}}",
        escapeJsonString(resultInput.token.send),
        escapeJsonString(resultInput.token.receive));
    outputStream << '}';

    resultOutput.payload = outputStream.str();
    return resultOutput;
}

[[nodiscard]] HttpMqttRequestData buildDisconnectV1Request(const std::string& clientId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.payload = std::format("{{\"clientId\":\"{}\"}}", escapeJsonString(clientId));
    requestData.resultCheck = [](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "disconnect result");
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildDisconnectV1Response() {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.payload.clear();

    return resultOutput;
}

[[nodiscard]] HttpMqttRequestData buildPublishV1Request(const HttpMqttPublishOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};

    const int qosNumber = static_cast<int>(optionsInput.message.qos());
    const bool retainEnabled = optionsInput.message.retain();
    const bool dupEnabled = optionsInput.dup.value_or(false);

    requestData.headers["qos"] = std::to_string(qosNumber);
    requestData.headers["retain"] = retainEnabled ? "1" : "0";
    requestData.headers["dup"] = dupEnabled ? "1" : "0";
    if (optionsInput.packetId.has_value()) {
        requestData.headers["packetid"] = std::to_string(*optionsInput.packetId);
    }

    if (optionsInput.message.rawPayload().has_value()) {
        requestData.payload = *optionsInput.message.rawPayload();
    } else {
        requestData.payload = std::format(
            "{{\"token\":\"{}\",\"message\":{{\"topic\":\"{}\",\"value\":{},\"reason\":{}}}}}",
            escapeJsonString(optionsInput.token),
            escapeJsonString(optionsInput.message.topic()),
            messageValueToJson(optionsInput.message.value()),
            reasonToJson(optionsInput.message));
    }

    requestData.resultCheck = [expectedQos = qosNumber, expectedPacketId = optionsInput.packetId](
                                  const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "publish result");
        validatePacketIdMatch(resultInput, expectedPacketId, "publish result");
        if (expectedQos == 1) {
            validateHeaderEquals(resultInput, "packet", "puback", "publish result");
        }
        if (expectedQos == 2) {
            validateHeaderEquals(resultInput, "packet", "pubrec", "publish result");
        }
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildPublishV1Response(const HttpMqttHeaders& headersInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.payload.clear();

    const auto qosHeader = tryReadHeaderValue(headersInput, "qos");
    const auto retainHeader = tryReadHeaderValue(headersInput, "retain");
    if (qosHeader.has_value()) {
        resultOutput.headers["qos"] = *qosHeader;
    }
    if (retainHeader.has_value()) {
        resultOutput.headers["retain"] = *retainHeader;
    }

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    int qosNumber = 0;
    if (qosHeader.has_value()) {
        const auto parseResult = std::from_chars(qosHeader->data(), qosHeader->data() + qosHeader->size(), qosNumber);
        if (parseResult.ec != std::errc{} || parseResult.ptr != qosHeader->data() + qosHeader->size()) {
            qosNumber = 0;
        }
    }

    if (qosNumber == 1) {
        resultOutput.headers["packet"] = "puback";
    } else if (qosNumber == 2) {
        resultOutput.headers["packet"] = "pubrec";
    }

    return resultOutput;
}

[[nodiscard]] HttpMqttRequestData buildPubrelV1Request(const HttpMqttPubrelOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardTextHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    if (optionsInput.packetId.has_value()) {
        requestData.headers["packetid"] = std::to_string(*optionsInput.packetId);
    }

    requestData.payload = std::format("{{\"token\":\"{}\"}}", escapeJsonString(optionsInput.token));
    requestData.resultCheck = [expectedPacketId = optionsInput.packetId](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "pubrel result");
        validatePacketIdMatch(resultInput, expectedPacketId, "pubrel result");
        validateHeaderEquals(resultInput, "packet", "pubcomp", "pubrel result");
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildPubrelV1Response(const HttpMqttHeaders& headersInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "pubcomp";
    resultOutput.payload.clear();

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    return resultOutput;
}

[[nodiscard]] HttpMqttRequestData buildSubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    const std::uint16_t packetId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.headers["packetid"] = std::to_string(packetId);
    requestData.payload = std::format(
        "{{\"clientId\":\"{}\",\"topics\":{}}}",
        escapeJsonString(clientId),
        serializeTopics(topicsInput));

    requestData.resultCheck = [expectedPacketId = packetId](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusOk, "subscribe result");
        validateContentTypeJson(resultInput, "subscribe result");
        validateHeaderEquals(resultInput, "packet", "suback", "subscribe result");
        validatePacketIdMatch(resultInput, expectedPacketId, "subscribe result");

        const std::vector<int> qosValues = parseIntegerArrayPayload(resultInput.payload);
        for (const int qosValue : qosValues) {
            if (qosValue != 0 && qosValue != 1 && qosValue != 2 &&
                qosValue != k_subscribeCodeFailLegacy && qosValue != k_subscribeCodeFailModern) {
                throw std::runtime_error{"subscribe result: unsupported qos return code"};
            }
        }
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildSubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttSubscribeResult& resultInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "suback";

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    resultOutput.payload = std::format("{{\"qos\":{}}}", serializeUInt8Array(resultInput));
    return resultOutput;
}

[[nodiscard]] HttpMqttRequestData buildUnsubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    const std::uint16_t packetId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.headers["packetid"] = std::to_string(packetId);
    requestData.payload = std::format(
        "{{\"topics\":{},\"clientId\":\"{}\"}}",
        serializeTopics(topicsInput),
        escapeJsonString(clientId));

    requestData.resultCheck = [expectedPacketId = packetId](const HttpMqttResult& resultInput) {
        if (resultInput.statusCode != k_httpStatusOk &&
            resultInput.statusCode != k_httpStatusNoContent) {
            throw std::runtime_error{"unsubscribe result: invalid status"};
        }

        validateContentTypeJson(resultInput, "unsubscribe result");
        validateHeaderEquals(resultInput, "packet", "unsuback", "unsubscribe result");
        validatePacketIdMatch(resultInput, expectedPacketId, "unsubscribe result");

        if (resultInput.statusCode == k_httpStatusNoContent && resultInput.payload.empty()) {
            return;
        }

        const std::vector<int> returnCodes = parseIntegerArrayPayload(resultInput.payload);
        for (const int returnCode : returnCodes) {
            if (returnCode != 0 && returnCode != k_unsubscribeNoSubscription) {
                throw std::runtime_error{"unsubscribe result: unsupported return code"};
            }
        }
    };

    return requestData;
}

[[nodiscard]] HttpMqttResult buildUnsubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttUnsubscribeResult& resultInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "unsuback";

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    resultOutput.payload = serializeUInt8Array(resultInput);
    return resultOutput;
}

} // namespace

HttpMqttInterfaceHandlerRegistry makeHttpMqttInterfaceHandlerRegistryV1() {
    HttpMqttInterfaceHandlerRegistry registry{};

    registry.connectRequests[std::string{k_versionValue}] = buildConnectV1Request;
    registry.connectResponses[std::string{k_versionValue}] = buildConnectV1Response;

    registry.disconnectRequests[std::string{k_versionValue}] = buildDisconnectV1Request;
    registry.disconnectResponses[std::string{k_versionValue}] = buildDisconnectV1Response;

    registry.publishRequests[std::string{k_versionValue}] = buildPublishV1Request;
    registry.publishResponses[std::string{k_versionValue}] = buildPublishV1Response;

    registry.pubrelRequests[std::string{k_versionValue}] = buildPubrelV1Request;
    registry.pubrelResponses[std::string{k_versionValue}] = buildPubrelV1Response;

    registry.subscribeRequests[std::string{k_versionValue}] = buildSubscribeV1Request;
    registry.subscribeResponses[std::string{k_versionValue}] = buildSubscribeV1Response;

    registry.unsubscribeRequests[std::string{k_versionValue}] = buildUnsubscribeV1Request;
    registry.unsubscribeResponses[std::string{k_versionValue}] = buildUnsubscribeV1Response;

    return registry;
}

HttpMqttInterfaces makeHttpMqttInterfacesV1() {
    return HttpMqttInterfaces{makeHttpMqttInterfaceHandlerRegistryV1()};
}

HttpMqttResult handlePublishCompatibilityRequest(
    const HttpMqttInterfaces& interfaces,
    const HttpMqttPublishCompatibilityRequest& requestInput,
    const HttpMqttPublishCompatibilityConfig& configInput,
    const HttpMqttPublishCompatibilityForwarder& forwarder) {
    if (!isSupportedCompatibilityRoute(requestInput.method, requestInput.endpoint, configInput)) {
        return makeCompatibilityErrorResponse(k_httpStatusMethodNotAllowed, "unsupported_method");
    }

    const HttpMqttHeaders normalizedFields = normalizeHeaderKeys(requestInput.fields);
    const std::optional<CompatibilityParsedFields> parsedFields = tryParseCompatibilityFields(normalizedFields);
    if (!parsedFields.has_value()) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "invalid_json");
    }

    CompatibilityParsedFields resolvedFields = *parsedFields;
    if (!applyCompatibilityBodyFallback(resolvedFields, requestInput.body)) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "invalid_json");
    }

    if (const std::optional<HttpMqttResult> topicError = validateCompatibilityTopic(resolvedFields);
        topicError.has_value()) {
        return *topicError;
    }

    const Message mappedMessage = buildCompatibilityMessage(resolvedFields);

    const HttpMqttPublishOptions mappedOptions{
        .token = requestInput.token,
        .message = mappedMessage,
        .dup = std::nullopt,
        .packetId = std::nullopt};
    const HttpMqttRequestData mappedRequest = interfaces.publish(k_versionValue, mappedOptions);

    const HttpMqttResult downstreamResult =
        tryForwardCompatibilityPublish(forwarder, mappedRequest, mappedMessage);

    return adaptCompatibilityResponse(downstreamResult, configInput);
}

} // namespace yaha
