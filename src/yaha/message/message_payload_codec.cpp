#include "yaha/message/message_payload_codec.h"

#include <cctype>
#include <cmath>

#include "json/json_value.h"

namespace yaha {

namespace {

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

[[nodiscard]] std::string quoteJsonString(const std::string_view textValue) {
    return mqtt::json::JsonValue{std::string{textValue}}.stringify();
}

[[nodiscard]] std::optional<Value> parseValueFromJson(const mqtt::json::JsonValue& valueNode) {
    if (valueNode.is_string()) {
        return Value{valueNode.as_string()};
    }

    if (valueNode.is_number()) {
        const double numericValue = valueNode.as_number();
        if (!std::isfinite(numericValue)) {
            return std::nullopt;
        }
        return Value{numericValue};
    }

    if (valueNode.is_boolean()) {
        return Value{valueNode.as_boolean() ? std::string{"true"} : std::string{"false"}};
    }

    if (valueNode.is_null()) {
        return Value{std::string{"null"}};
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<ReasonList> parseReasonArrayValue(const mqtt::json::JsonValue& reasonArrayValue) {
    if (!reasonArrayValue.is_array()) {
        return std::nullopt;
    }

    ReasonList reasonEntries{};
    reasonEntries.reserve(reasonArrayValue.as_array().size());

    for (const auto& reasonNode : reasonArrayValue.as_array()) {
        if (!reasonNode.is_object()) {
            return std::nullopt;
        }

        const auto& reasonObject = reasonNode.as_object();
        const auto messageIterator = reasonObject.find("message");
        if (messageIterator == reasonObject.end()
            || !messageIterator->second.is_string()) {
            return std::nullopt;
        }

        const std::string messageText = messageIterator->second.as_string();
        if (messageText.empty()) {
            return std::nullopt;
        }

        std::string timestampText{};
        const auto timestampIterator = reasonObject.find("timestamp");
        if (timestampIterator != reasonObject.end()
            && timestampIterator->second.is_string()) {
            timestampText = timestampIterator->second.as_string();
        }

        reasonEntries.push_back(ReasonEntry{.message = messageText, .timestamp = timestampText});
    }

    return reasonEntries;
}

void appendReasonEntries(const ReasonList& reasonEntries, Message& outputMessage) {
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
    const std::string quoted = mqtt::json::JsonValue{std::string{textValue}}.stringify();
    if (quoted.size() < 2U || quoted.front() != '"' || quoted.back() != '"') {
        return std::string{};
    }
    return quoted.substr(1U, quoted.size() - 2U);
}

std::string serializeReasonArrayOldestFirst(const ReasonList& reasonEntries) {
    std::string reasonJson{"["};

    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        if (reverseIndex != reasonEntries.size()) {
            reasonJson.push_back(',');
        }

        const ReasonEntry& reasonEntry = reasonEntries[reverseIndex - 1U];
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

    if (std::holds_alternative<std::string>(messageValue.value())) {
        payload += std::string{",\"value\":"}
            + quoteJsonString(std::get<std::string>(messageValue.value()));
    } else {
        payload += std::string{",\"value\":"}
            + std::to_string(std::get<double>(messageValue.value()));
    }

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

    const auto parsedValue = mqtt::json::JsonValue::try_parse(tokenText);
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }
    if (parsedValue->is_object() || parsedValue->is_array()) {
        return std::nullopt;
    }

    return parseValueFromJson(*parsedValue);
}

std::optional<ReasonList> parseReasonArray(const std::string_view reasonArrayToken) {
    const std::string reasonArrayText = trimCopy(std::string{reasonArrayToken});

    const auto parsedValue = mqtt::json::JsonValue::try_parse(reasonArrayText);
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }

    return parseReasonArrayValue(*parsedValue);
}

std::optional<Message> parseEnvelopePayload(const std::string& payloadText,
                                            const std::string& mqttTopic,
                                            const Qos qosLevel,
                                            const bool retainFlag,
                                            const bool dupFlag) {
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedRoot.has_value() || !parsedRoot->is_object()) {
        return std::nullopt;
    }

    const auto& rootObject = parsedRoot->as_object();
    const auto messageIterator = rootObject.find("message");
    if (messageIterator == rootObject.end() || !messageIterator->second.is_object()) {
        return std::nullopt;
    }

    const auto& messageObject = messageIterator->second.as_object();

    const auto topicIterator = messageObject.find("topic");
    if (topicIterator == messageObject.end() || !topicIterator->second.is_string()) {
        return std::nullopt;
    }

    const std::string parsedTopic = topicIterator->second.as_string();
    if (parsedTopic.empty() || parsedTopic != mqttTopic) {
        return std::nullopt;
    }

    const auto valueIterator = messageObject.find("value");
    if (valueIterator == messageObject.end()) {
        return std::nullopt;
    }

    const std::optional<Value> parsedValue = parseValueFromJson(valueIterator->second);
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }

    Message parsedMessage{parsedTopic, *parsedValue, qosLevel, retainFlag, dupFlag};

    const auto reasonIterator = messageObject.find("reason");
    if (reasonIterator == messageObject.end()) {
        parsedMessage.setRawPayload(payloadText);
        return parsedMessage;
    }

    if (reasonIterator->second.is_array()) {
        const std::optional<ReasonList> reasonEntries = parseReasonArrayValue(reasonIterator->second);
        if (!reasonEntries.has_value()) {
            return std::nullopt;
        }
        appendReasonEntries(*reasonEntries, parsedMessage);
        parsedMessage.setRawPayload(payloadText);
        return parsedMessage;
    }

    const std::optional<Value> reasonValue = parseValueFromJson(reasonIterator->second);
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
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedRoot.has_value() || !parsedRoot->is_object()) {
        return false;
    }

    const auto& rootObject = parsedRoot->as_object();
    const auto messageIterator = rootObject.find("message");
    if (messageIterator == rootObject.end() || !messageIterator->second.is_object()) {
        return false;
    }

    const auto& messageObject = messageIterator->second.as_object();
    const auto topicIterator = messageObject.find("topic");
    if (topicIterator == messageObject.end()
        || !topicIterator->second.is_string()
        || topicIterator->second.as_string().empty()) {
        return false;
    }

    const auto valueIterator = messageObject.find("value");
    if (valueIterator == messageObject.end()) {
        return false;
    }

    const std::optional<Value> parsedValue = parseValueFromJson(valueIterator->second);
    return parsedValue.has_value();
}

} // namespace yaha
