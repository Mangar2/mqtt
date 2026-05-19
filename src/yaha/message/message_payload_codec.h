#pragma once

/**
 * @file message_payload_codec.h
 * @brief Shared YAHA message payload parser/builder utilities.
 */

#include "yaha/message/message.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

/**
 * @brief Escapes a text token for use inside JSON string value content.
 * @param textValue Source text that should be escaped.
 * @return Escaped JSON string content without surrounding quotes.
 */
[[nodiscard]] std::string escapeJsonString(std::string_view textValue);

/**
 * @brief Serializes reason entries to JSON array in oldest-first order.
 * @param reasonEntries Reason chain in YAHA internal order (most-recent first).
 * @return JSON array text that can be embedded in an envelope payload.
 */
[[nodiscard]] std::string serializeReasonArrayOldestFirst(
    const std::vector<ReasonEntry>& reasonEntries);

/**
 * @brief Builds canonical YAHA envelope JSON payload from a Message.
 * @param messageValue Input message to serialize.
 * @return Canonical JSON payload with top-level message envelope.
 */
[[nodiscard]] std::string buildEnvelopePayload(const Message& messageValue);

/**
 * @brief Parses a raw JSON value token into YAHA Value.
 * @param valueToken Raw token text (quoted string or numeric token).
 * @return Parsed value or empty optional for malformed token.
 */
[[nodiscard]] std::optional<Value> parseValueToken(std::string_view valueToken);

/**
 * @brief Parses a JSON reason array into ReasonEntry values.
 * @param reasonArrayToken Raw reason array text.
 * @return Parsed reason entries in wire order or empty optional on parse failure.
 */
[[nodiscard]] std::optional<std::vector<ReasonEntry>> parseReasonArray(
    std::string_view reasonArrayToken);

/**
 * @brief Parses a YAHA envelope payload to Message when format is valid.
 * @param payloadText Raw payload bytes interpreted as UTF-8 text.
 * @param mqttTopic MQTT topic from transport packet.
 * @param qosLevel MQTT QoS translated to YAHA enum.
 * @param retainFlag MQTT retain flag from transport packet.
 * @param dupFlag MQTT dup flag from transport packet.
 * @return Parsed Message including metadata; empty optional if payload is not a valid envelope.
 */
[[nodiscard]] std::optional<Message> parseEnvelopePayload(
    const std::string& payloadText,
    const std::string& mqttTopic,
    Qos qosLevel,
    bool retainFlag,
    bool dupFlag);

/**
 * @brief Checks if payload contains required canonical envelope fields.
 * @param payloadText Raw payload text.
 * @return True when payload contains message object with required topic/value fields.
 */
[[nodiscard]] bool validateEnvelopeShape(std::string_view payloadText);

} // namespace yaha
