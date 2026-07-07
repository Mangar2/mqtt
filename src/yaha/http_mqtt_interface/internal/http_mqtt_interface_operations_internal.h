#pragma once

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

namespace http_mqtt_ops_internal {

inline constexpr std::string_view k_versionValue{"1.0"};
inline constexpr std::string_view k_contentTypeJsonPrefix{"application/json"};
inline constexpr int k_httpStatusOk{200};
inline constexpr int k_httpStatusBadRequest{400};
inline constexpr int k_httpStatusMethodNotAllowed{405};
inline constexpr int k_httpStatusNoContent{204};
inline constexpr int k_httpStatusInternalServerError{500};
inline constexpr int k_connectCodeMax{5};
inline constexpr int k_subscribeCodeFailLegacy{127};
inline constexpr int k_subscribeCodeFailModern{128};
inline constexpr int k_unsubscribeNoSubscription{17};
inline constexpr std::size_t k_escapeReservePadding{8U};
inline constexpr std::string_view k_publishIngressReasonMessage{"Request by User"};

[[nodiscard]] std::string toUpperCopy(std::string_view valueText);
[[nodiscard]] std::string decodeTopicSlashEscapes(std::string_view topicInput);
[[nodiscard]] std::string escapeJsonString(std::string_view valueText);
[[nodiscard]] std::string messageValueToJson(const Value& valueInput);
[[nodiscard]] std::string reasonToJson(const Message& messageInput);
[[nodiscard]] std::optional<std::string> tryExtractStringField(
    std::string_view objectText,
    std::string_view keyName);
[[nodiscard]] std::optional<int> tryExtractIntegerField(
    std::string_view objectText,
    std::string_view keyName);
[[nodiscard]] std::optional<std::string> extractRawToken(
    std::string_view objectText,
    std::string_view keyName);
[[nodiscard]] std::optional<Value> parseJsonValueToken(std::string_view tokenInput);
[[nodiscard]] std::optional<ReasonList> parseReasonArray(std::string_view arrayText);
void appendReasonsPreservingOrder(Message& messageOutput, const ReasonList& reasonEntries);

[[nodiscard]] std::optional<Qos> parseQosField(std::string_view textInput);
[[nodiscard]] std::optional<bool> parseRetainField(std::string_view textInput);

[[nodiscard]] std::string serializeTopics(const HttpMqttTopics& topicsInput);
[[nodiscard]] std::string serializeUInt8Array(const std::vector<std::uint8_t>& valuesInput);
[[nodiscard]] std::vector<int> parseIntegerArrayPayload(std::string_view payloadText);

void validateStatusCode(const HttpMqttResult& resultInput, int expectedStatus, std::string_view contextText);
void validateContentTypeJson(const HttpMqttResult& resultInput, std::string_view contextText);
void validateHeaderEquals(
    const HttpMqttResult& resultInput,
    std::string_view headerName,
    std::string_view expectedValue,
    std::string_view contextText);
void validatePacketIdMatch(
    const HttpMqttResult& resultInput,
    std::optional<std::uint16_t> expectedPacketId,
    std::string_view contextText);

} // namespace http_mqtt_ops_internal

[[nodiscard]] HttpMqttRequestData buildConnectV1Request(const HttpMqttConnectOptions& optionsInput);
[[nodiscard]] HttpMqttResult buildConnectV1Response(const HttpMqttConnectResult& resultInput);
[[nodiscard]] HttpMqttRequestData buildDisconnectV1Request(const std::string& clientId);
[[nodiscard]] HttpMqttResult buildDisconnectV1Response();
[[nodiscard]] HttpMqttRequestData buildPublishV1Request(const HttpMqttPublishOptions& optionsInput);
[[nodiscard]] HttpMqttResult buildPublishV1Response(const HttpMqttHeaders& headersInput);
[[nodiscard]] HttpMqttRequestData buildPubrelV1Request(const HttpMqttPubrelOptions& optionsInput);
[[nodiscard]] HttpMqttResult buildPubrelV1Response(const HttpMqttHeaders& headersInput);

[[nodiscard]] HttpMqttRequestData buildSubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    std::uint16_t packetId);
[[nodiscard]] HttpMqttResult buildSubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttSubscribeResult& resultInput);
[[nodiscard]] HttpMqttRequestData buildUnsubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    std::uint16_t packetId);
[[nodiscard]] HttpMqttResult buildUnsubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttUnsubscribeResult& resultInput);

} // namespace yaha
