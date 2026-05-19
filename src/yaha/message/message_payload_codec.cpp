#include "yaha/message/message_payload_codec.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace yaha {

namespace {

constexpr unsigned char k_ascii_control_max{0x20U};
constexpr unsigned char k_low_nibble_mask{0x0FU};
constexpr std::uint32_t k_ascii_max_single_byte_codepoint{0x7FU};
constexpr std::uint32_t k_hex_alpha_offset{10U};

struct ParsedRange {
    std::size_t start{0U};
    std::size_t end{0U};
};

[[nodiscard]] std::optional<std::uint32_t> decodeHexCharacter(const char hexCharacter) {
    if (hexCharacter >= '0' && hexCharacter <= '9') {
        return static_cast<std::uint32_t>(hexCharacter - '0');
    }
    if (hexCharacter >= 'a' && hexCharacter <= 'f') {
        return k_hex_alpha_offset + static_cast<std::uint32_t>(hexCharacter - 'a');
    }
    if (hexCharacter >= 'A' && hexCharacter <= 'F') {
        return k_hex_alpha_offset + static_cast<std::uint32_t>(hexCharacter - 'A');
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> decodeUnicodeEscapeCodePoint(
    const std::string& objectText,
    const std::size_t codePointStart) {
    if (codePointStart + 3U >= objectText.size()) {
        return std::nullopt;
    }

    std::uint32_t codePoint = 0U;
    for (std::size_t hexOffset = 0U; hexOffset < 4U; ++hexOffset) {
        const auto hexValue = decodeHexCharacter(objectText[codePointStart + hexOffset]);
        if (!hexValue.has_value()) {
            return std::nullopt;
        }

        codePoint <<= 4U;
        codePoint += *hexValue;
    }

    return codePoint;
}

[[nodiscard]] std::optional<char> decodeEscapedCharacter(const char escapedCharacter) {
    switch (escapedCharacter) {
        case '"':
        case '\\':
        case '/':
            return escapedCharacter;
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        default:
            return std::nullopt;
    }
}

[[nodiscard]] bool parseEscapedSequence(const std::string& objectText,
                                        std::size_t& cursorPosition,
                                        std::string& parsedValue) {
    if (cursorPosition >= objectText.size()) {
        return false;
    }

    const char escapedCharacter = objectText[cursorPosition];
    if (escapedCharacter == 'u') {
        const auto codePoint = decodeUnicodeEscapeCodePoint(objectText, cursorPosition + 1U);
        if (!codePoint.has_value()) {
            return false;
        }

        if (*codePoint <= k_ascii_max_single_byte_codepoint) {
            parsedValue.push_back(static_cast<char>(*codePoint));
        } else {
            parsedValue.push_back('?');
        }
        cursorPosition += 4U;
        return true;
    }

    const auto decodedCharacter = decodeEscapedCharacter(escapedCharacter);
    if (!decodedCharacter.has_value()) {
        return false;
    }

    parsedValue.push_back(*decodedCharacter);
    return true;
}

[[nodiscard]] std::string quoteJsonString(const std::string_view textValue) {
    std::string result{"\""};
    constexpr std::array<char, 16U> k_hex_digits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    for (const unsigned char currentChar : textValue) {
        switch (currentChar) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            default:
                if (currentChar < k_ascii_control_max) {
                    result += "\\u00";
                    result.push_back(k_hex_digits[(currentChar >> 4U) & k_low_nibble_mask]);
                    result.push_back(k_hex_digits[currentChar & k_low_nibble_mask]);
                } else {
                    result.push_back(static_cast<char>(currentChar));
                }
                break;
        }
    }

    result.push_back('"');
    return result;
}

[[nodiscard]] std::string valueToJsonToken(const Value& valueVariant) {
    if (std::holds_alternative<std::string>(valueVariant)) {
        return quoteJsonString(std::get<std::string>(valueVariant));
    }

    return std::to_string(std::get<double>(valueVariant));
}

[[nodiscard]] std::optional<ParsedRange> tryFindObjectRange(const std::string& text,
                                                            const std::string& keyName) {
    const std::string keyToken = "\"" + keyName + "\"";
    const std::size_t keyPosition = text.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPosition = text.find(':', keyPosition + keyToken.size());
    if (cursorPosition == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPosition;

    while (cursorPosition < text.size()
           && std::isspace(static_cast<unsigned char>(text[cursorPosition])) != 0) {
        ++cursorPosition;
    }
    if (cursorPosition >= text.size() || text[cursorPosition] != '{') {
        return std::nullopt;
    }

    int depthValue = 0;
    for (std::size_t indexPosition = cursorPosition; indexPosition < text.size(); ++indexPosition) {
        if (text[indexPosition] == '{') {
            ++depthValue;
        } else if (text[indexPosition] == '}') {
            --depthValue;
            if (depthValue == 0) {
                return ParsedRange{.start = cursorPosition, .end = indexPosition};
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ParsedRange> tryFindArrayRange(const std::string& text,
                                                           const std::string& keyName) {
    const std::string keyToken = "\"" + keyName + "\"";
    const std::size_t keyPosition = text.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPosition = text.find(':', keyPosition + keyToken.size());
    if (cursorPosition == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPosition;

    while (cursorPosition < text.size()
           && std::isspace(static_cast<unsigned char>(text[cursorPosition])) != 0) {
        ++cursorPosition;
    }
    if (cursorPosition >= text.size() || text[cursorPosition] != '[') {
        return std::nullopt;
    }

    int depthValue = 0;
    for (std::size_t indexPosition = cursorPosition; indexPosition < text.size(); ++indexPosition) {
        if (text[indexPosition] == '[') {
            ++depthValue;
        } else if (text[indexPosition] == ']') {
            --depthValue;
            if (depthValue == 0) {
                return ParsedRange{.start = cursorPosition, .end = indexPosition};
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> tryExtractKeyStringValue(const std::string& objectText,
                                                                  const std::string& keyName) {
    const std::string keyToken = "\"" + keyName + "\"";
    const std::size_t keyPosition = objectText.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPosition = objectText.find(':', keyPosition + keyToken.size());
    if (cursorPosition == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPosition;

    while (cursorPosition < objectText.size()
           && std::isspace(static_cast<unsigned char>(objectText[cursorPosition])) != 0) {
        ++cursorPosition;
    }
    if (cursorPosition >= objectText.size() || objectText[cursorPosition] != '"') {
        return std::nullopt;
    }

    ++cursorPosition;
    std::string parsedValue{};
    while (cursorPosition < objectText.size()) {
        const char currentCharacter = objectText[cursorPosition];
        if (currentCharacter == '\\') {
            ++cursorPosition;
            if (cursorPosition >= objectText.size()) {
                return std::nullopt;
            }

            if (!parseEscapedSequence(objectText, cursorPosition, parsedValue)) {
                return std::nullopt;
            }
            ++cursorPosition;
            continue;
        }
        if (currentCharacter == '"') {
            return parsedValue;
        }
        parsedValue.push_back(currentCharacter);
        ++cursorPosition;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ParsedRange> tryExtractKeyValueToken(const std::string& objectText,
                                                                 const std::string& keyName) {
    const std::string keyToken = "\"" + keyName + "\"";
    const std::size_t keyPosition = objectText.find(keyToken);
    if (keyPosition == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPosition = objectText.find(':', keyPosition + keyToken.size());
    if (cursorPosition == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPosition;

    while (cursorPosition < objectText.size()
           && std::isspace(static_cast<unsigned char>(objectText[cursorPosition])) != 0) {
        ++cursorPosition;
    }
    if (cursorPosition >= objectText.size()) {
        return std::nullopt;
    }

    const std::size_t tokenStart = cursorPosition;
    if (objectText[cursorPosition] == '"') {
        ++cursorPosition;
        while (cursorPosition < objectText.size()) {
            if (objectText[cursorPosition] == '\\') {
                cursorPosition += 2U;
                continue;
            }
            if (objectText[cursorPosition] == '"') {
                return ParsedRange{.start = tokenStart, .end = cursorPosition};
            }
            ++cursorPosition;
        }
        return std::nullopt;
    }

    while (cursorPosition < objectText.size()
           && objectText[cursorPosition] != ','
           && objectText[cursorPosition] != '}') {
        ++cursorPosition;
    }

    const std::size_t tokenEnd = cursorPosition == 0U ? 0U : cursorPosition - 1U;
    if (tokenEnd < tokenStart) {
        return std::nullopt;
    }

    return ParsedRange{.start = tokenStart, .end = tokenEnd};
}

[[nodiscard]] std::string trimCopy(const std::string& textValue) {
    std::size_t beginIndex = 0U;
    while (beginIndex < textValue.size()
           && std::isspace(static_cast<unsigned char>(textValue[beginIndex])) != 0) {
        ++beginIndex;
    }

    std::size_t endIndex = textValue.size();
    while (endIndex > beginIndex
           && std::isspace(static_cast<unsigned char>(textValue[endIndex - 1U])) != 0) {
        --endIndex;
    }

    return textValue.substr(beginIndex, endIndex - beginIndex);
}

[[nodiscard]] std::optional<std::size_t> findReasonObjectEnd(const std::string& reasonArrayText,
                                                             std::size_t objectStart) {
    if (objectStart >= reasonArrayText.size() || reasonArrayText[objectStart] != '{') {
        return std::nullopt;
    }

    int depthValue = 0;
    for (std::size_t objectEnd = objectStart; objectEnd < reasonArrayText.size(); ++objectEnd) {
        if (reasonArrayText[objectEnd] == '{') {
            ++depthValue;
        } else if (reasonArrayText[objectEnd] == '}') {
            --depthValue;
            if (depthValue == 0) {
                return objectEnd;
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ReasonEntry> tryParseReasonObject(const std::string& reasonObjectText) {
    const std::optional<std::string> messageText =
        tryExtractKeyStringValue(reasonObjectText, "message");
    if (!messageText.has_value() || messageText->empty()) {
        return std::nullopt;
    }

    const std::optional<std::string> timestampText =
        tryExtractKeyStringValue(reasonObjectText, "timestamp");
    return ReasonEntry{.message = *messageText, .timestamp = timestampText.value_or(std::string{})};
}

void appendReasonEntries(const std::vector<ReasonEntry>& reasonEntries,
                         Message& outputMessage) {
    for (const ReasonEntry& reasonEntry : reasonEntries) {
        if (reasonEntry.timestamp.empty()) {
            outputMessage.addReason(reasonEntry.message);
        } else {
            outputMessage.addReason(reasonEntry.message, reasonEntry.timestamp);
        }
    }
}

} // namespace

std::string escapeJsonString(const std::string_view textValue) {
    const std::string quoted = quoteJsonString(textValue);
    if (quoted.size() < 2U) {
        return std::string{};
    }
    return quoted.substr(1U, quoted.size() - 2U);
}

std::string serializeReasonArrayOldestFirst(const std::vector<ReasonEntry>& reasonEntries) {
    std::string reasonJson{"["};
    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        if (reverseIndex != reasonEntries.size()) {
            reasonJson.push_back(',');
        }
        const auto& reasonEntry = reasonEntries[reverseIndex - 1U];
        reasonJson += std::string{"{\"timestamp\":"}
            + quoteJsonString(reasonEntry.timestamp)
            + std::string{",\"message\":"}
            + quoteJsonString(reasonEntry.message)
            + "}";
    }
    reasonJson.push_back(']');
    return reasonJson;
}

std::string buildEnvelopePayload(const Message& messageValue) {
    std::string payload{"{\"message\":{"};
    payload += std::string{"\"topic\":"} + quoteJsonString(messageValue.topic());
    payload += std::string{",\"value\":"} + valueToJsonToken(messageValue.value());
    if (!messageValue.reason().empty()) {
        payload += std::string{",\"reason\":"}
            + serializeReasonArrayOldestFirst(messageValue.reason());
    }
    payload += "}}";
    return payload;
}

std::optional<Value> parseValueToken(const std::string_view valueToken) {
    const std::string tokenText = trimCopy(std::string{valueToken});
    if (tokenText.empty()) {
        return std::nullopt;
    }

    if (tokenText.front() == '"') {
        if (tokenText.size() < 2U || tokenText.back() != '"') {
            return std::nullopt;
        }
        const std::optional<std::string> textValue =
            tryExtractKeyStringValue("{\"temp\":" + tokenText + "}", "temp");
        if (!textValue.has_value()) {
            return std::nullopt;
        }
        return Value{*textValue};
    }

    if (tokenText == "true" || tokenText == "false" || tokenText == "null") {
        return Value{tokenText};
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(tokenText.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0' || !std::isfinite(parsedNumber)) {
        return std::nullopt;
    }

    return Value{parsedNumber};
}

std::optional<std::vector<ReasonEntry>> parseReasonArray(const std::string_view reasonArrayToken) {
    const std::string reasonArrayText = trimCopy(std::string{reasonArrayToken});
    if (reasonArrayText.size() < 2U
        || reasonArrayText.front() != '['
        || reasonArrayText.back() != ']') {
        return std::nullopt;
    }

    std::vector<ReasonEntry> reasonEntries{};
    std::size_t cursorPosition = 1U;
    while (cursorPosition + 1U < reasonArrayText.size()) {
        while (cursorPosition + 1U < reasonArrayText.size()
               && (std::isspace(static_cast<unsigned char>(reasonArrayText[cursorPosition])) != 0
                   || reasonArrayText[cursorPosition] == ',')) {
            ++cursorPosition;
        }

        if (cursorPosition + 1U >= reasonArrayText.size()
            || reasonArrayText[cursorPosition] == ']') {
            break;
        }

        const std::optional<std::size_t> objectEnd =
            findReasonObjectEnd(reasonArrayText, cursorPosition);
        if (!objectEnd.has_value()) {
            return std::nullopt;
        }

        const std::string reasonObjectText = reasonArrayText.substr(
            cursorPosition, *objectEnd - cursorPosition + 1U);
        const std::optional<ReasonEntry> reasonEntry = tryParseReasonObject(reasonObjectText);
        if (!reasonEntry.has_value()) {
            return std::nullopt;
        }

        reasonEntries.push_back(*reasonEntry);
        cursorPosition = *objectEnd + 1U;
    }

    return reasonEntries;
}

std::optional<Message> parseEnvelopePayload(const std::string& payloadText,
                                            const std::string& mqttTopic,
                                            const Qos qosLevel,
                                            const bool retainFlag,
                                            const bool dupFlag) {
    const std::optional<ParsedRange> messageRange =
        tryFindObjectRange(payloadText, "message");
    if (!messageRange.has_value()) {
        return std::nullopt;
    }

    const std::string bodyText = payloadText.substr(
        messageRange->start, messageRange->end - messageRange->start + 1U);

    const std::optional<std::string> parsedTopic =
        tryExtractKeyStringValue(bodyText, "topic");
    if (!parsedTopic.has_value() || parsedTopic->empty()) {
        return std::nullopt;
    }
    if (*parsedTopic != mqttTopic) {
        return std::nullopt;
    }

    const std::optional<ParsedRange> valueRange =
        tryExtractKeyValueToken(bodyText, "value");
    if (!valueRange.has_value()) {
        return std::nullopt;
    }

    const std::optional<Value> parsedValue = parseValueToken(
        std::string_view{bodyText}.substr(valueRange->start, valueRange->end - valueRange->start + 1U));
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }

    Message parsedMessage{*parsedTopic, *parsedValue, qosLevel, retainFlag, dupFlag};

    const std::optional<ParsedRange> reasonArrayRange =
        tryFindArrayRange(bodyText, "reason");
    if (reasonArrayRange.has_value()) {
        const std::string_view reasonArrayView = std::string_view{bodyText}.substr(
            reasonArrayRange->start,
            reasonArrayRange->end - reasonArrayRange->start + 1U);
        const std::optional<std::vector<ReasonEntry>> reasonEntries = parseReasonArray(reasonArrayView);
        if (!reasonEntries.has_value()) {
            return std::nullopt;
        }
        appendReasonEntries(*reasonEntries, parsedMessage);
        parsedMessage.setRawPayload(payloadText);
        return parsedMessage;
    }

    const std::optional<ParsedRange> reasonValueRange =
        tryExtractKeyValueToken(bodyText, "reason");
    if (!reasonValueRange.has_value()) {
        parsedMessage.setRawPayload(payloadText);
        return parsedMessage;
    }

    const std::optional<Value> reasonValue = parseValueToken(
        std::string_view{bodyText}.substr(
            reasonValueRange->start,
            reasonValueRange->end - reasonValueRange->start + 1U));
    if (!reasonValue.has_value()) {
        return std::nullopt;
    }
    if (std::holds_alternative<std::string>(*reasonValue)) {
        const auto& reasonText = std::get<std::string>(*reasonValue);
        if (!reasonText.empty()) {
            parsedMessage.addReason(reasonText);
        }
    }

    parsedMessage.setRawPayload(payloadText);
    return parsedMessage;
}

bool validateEnvelopeShape(const std::string_view payloadText) {
    const std::string payloadCopy{payloadText};
    const std::optional<ParsedRange> messageRange = tryFindObjectRange(payloadCopy, "message");
    if (!messageRange.has_value()) {
        return false;
    }

    const std::string bodyText = payloadCopy.substr(
        messageRange->start, messageRange->end - messageRange->start + 1U);
    const std::optional<std::string> parsedTopic = tryExtractKeyStringValue(bodyText, "topic");
    if (!parsedTopic.has_value() || parsedTopic->empty()) {
        return false;
    }

    const std::optional<ParsedRange> valueRange = tryExtractKeyValueToken(bodyText, "value");
    if (!valueRange.has_value()) {
        return false;
    }

    const std::optional<Value> parsedValue = parseValueToken(
        std::string_view{bodyText}.substr(valueRange->start, valueRange->end - valueRange->start + 1U));
    return parsedValue.has_value();
}

} // namespace yaha
