#include "yaha/serial_device/serial_device_parser.h"

#include "json/json_value.h"

#include <ranges>

namespace yaha {
namespace {

[[nodiscard]] std::optional<SerialDeviceEndpoint> tryParseEndpoint(const mqtt::json::JsonValue& value) {
    if (value.is_null()) {
        return SerialDeviceEndpoint{};
    }
    if (value.is_number()) {
        return SerialDeviceEndpoint{static_cast<std::int64_t>(value.as_number())};
    }
    if (value.is_string()) {
        return SerialDeviceEndpoint{value.as_string()};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SerialDeviceValue> tryParseValue(const mqtt::json::JsonValue& value) {
    if (value.is_number()) {
        return SerialDeviceValue{static_cast<std::int64_t>(value.as_number())};
    }
    if (value.is_string()) {
        return SerialDeviceValue{value.as_string()};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> tryParseString(const mqtt::json::JsonValue& value) {
    if (value.is_string()) {
        return value.as_string();
    }
    if (value.is_number()) {
        return std::to_string(static_cast<std::int64_t>(value.as_number()));
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t bitStringToValue(const std::string& bitString) {
    std::int64_t result{0};
    for (const char bitValue : std::views::reverse(bitString)) {
        result *= 2;
        if (bitValue == '1') {
            ++result;
        }
    }
    return result;
}

[[nodiscard]] std::optional<SerialDeviceMessage> tryParseArrayMessage(const mqtt::json::JsonValue& receivedObject) {
    if (!receivedObject.is_array() || receivedObject.size() < 3U) {
        return std::nullopt;
    }

    const auto senderValue = tryParseEndpoint(receivedObject.at(0));
    const auto commandValue = tryParseString(receivedObject.at(1));
    if (!senderValue.has_value() || !commandValue.has_value()) {
        return std::nullopt;
    }

    SerialDeviceMessage message{};
    message.interfaceName = commandValue.value() == "switch" ? "switch" : "i2c";
    message.sender = senderValue.value();
    message.receiver = SerialDeviceEndpoint{};
    message.command = commandValue.value();

    if (message.command == "switch" && receivedObject.at(2).is_string()) {
        message.value = SerialDeviceValue{bitStringToValue(receivedObject.at(2).as_string())};
        return message;
    }

    const auto parsedValue = tryParseValue(receivedObject.at(2));
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }
    message.value = parsedValue.value();
    return message;
}

[[nodiscard]] std::optional<SerialDeviceMessage> tryParseSerialObjectMessage(const mqtt::json::JsonValue& receivedObject) {
    if (!receivedObject.is_object() || !receivedObject.contains("S")) {
        return std::nullopt;
    }
    if (!receivedObject.contains("R") || !receivedObject.contains("K") || !receivedObject.contains("V")) {
        return std::nullopt;
    }

    const auto senderValue = tryParseEndpoint(receivedObject.at("S"));
    const auto receiverValue = tryParseEndpoint(receivedObject.at("R"));
    const auto commandValue = tryParseString(receivedObject.at("K"));
    const auto payloadValue = tryParseValue(receivedObject.at("V"));
    if (!senderValue.has_value() || !receiverValue.has_value() ||
        !commandValue.has_value() || !payloadValue.has_value()) {
        return std::nullopt;
    }

    return SerialDeviceMessage{
        .interfaceName = "serial",
        .sender = senderValue.value(),
        .receiver = receiverValue.value(),
        .command = commandValue.value(),
        .value = payloadValue.value(),
        .action = ""};
}

[[nodiscard]] std::optional<SerialDeviceMessage> tryParseFs20ObjectMessage(const mqtt::json::JsonValue& receivedObject) {
    if (!receivedObject.is_object() || !receivedObject.contains("Hauscode")) {
        return std::nullopt;
    }
    if (!receivedObject.contains("Adresse") || !receivedObject.contains("Befehl")) {
        return std::nullopt;
    }

    const auto houseCode = tryParseString(receivedObject.at("Hauscode"));
    const auto address = tryParseString(receivedObject.at("Adresse"));
    if (!houseCode.has_value() || !address.has_value() || !receivedObject.at("Befehl").is_number()) {
        return std::nullopt;
    }

    const std::string mappedValue = static_cast<std::int64_t>(receivedObject.at("Befehl").as_number()) == 0 ? "off" : "on";

    return SerialDeviceMessage{
        .interfaceName = "fs20",
        .sender = SerialDeviceEndpoint{},
        .receiver = SerialDeviceEndpoint{},
        .command = houseCode.value() + "/" + address.value(),
        .value = SerialDeviceValue{mappedValue},
        .action = "/set"};
}

[[nodiscard]] std::optional<SerialDeviceMessage> toMessage(const mqtt::json::JsonValue& receivedObject) {
    if (const auto arrayMessage = tryParseArrayMessage(receivedObject); arrayMessage.has_value()) {
        return arrayMessage;
    }
    if (const auto serialObjectMessage = tryParseSerialObjectMessage(receivedObject); serialObjectMessage.has_value()) {
        return serialObjectMessage;
    }
    if (const auto fs20ObjectMessage = tryParseFs20ObjectMessage(receivedObject); fs20ObjectMessage.has_value()) {
        return fs20ObjectMessage;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extractNextJsonObject(std::string& streamData) {
    if (streamData.empty()) {
        return std::nullopt;
    }

    const std::size_t firstBracketPosition = streamData.find_first_of("[{");
    if (firstBracketPosition == std::string::npos) {
        streamData.clear();
        return std::nullopt;
    }

    if (firstBracketPosition > 0U) {
        streamData.erase(0U, firstBracketPosition);
    }

    if (streamData.empty()) {
        return std::nullopt;
    }

    const char firstCharacter = streamData.front();
    const char closingCharacter = firstCharacter == '[' ? ']' : '}';
    const std::size_t closingBracketPosition = streamData.find(closingCharacter);
    if (closingBracketPosition == std::string::npos) {
        return std::nullopt;
    }

    std::string frameText = streamData.substr(0U, closingBracketPosition + 1U);
    streamData.erase(0U, closingBracketPosition + 1U);
    return frameText;
}

} // namespace

std::optional<SerialDeviceMessage> SerialDeviceStreamParser::parseChunk(std::string_view chunkText) {
    receivedDataAsString_.append(chunkText);

    while (true) {
        const std::optional<std::string> frameText = extractNextJsonObject(receivedDataAsString_);
        if (!frameText.has_value()) {
            return std::nullopt;
        }

        const std::optional<mqtt::json::JsonValue> maybeObject = mqtt::json::JsonValue::try_parse(frameText.value());
        if (!maybeObject.has_value()) {
            continue;
        }

        const std::optional<SerialDeviceMessage> maybeMessage = toMessage(maybeObject.value());
        if (maybeMessage.has_value()) {
            return maybeMessage;
        }
    }
}

} // namespace yaha
