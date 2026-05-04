#pragma once

/**
 * @file http_mqtt_interface_contracts.h
 * @brief Shared contract types and validation helpers for HTTP MQTT interface.
 */

#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace yaha {

/**
 * @brief HTTP header map used by HTTP MQTT interface operations.
 */
using HttpMqttHeaders = std::map<std::string, std::string>;

/**
 * @brief Topic-to-qos mapping used by subscribe and unsubscribe requests.
 */
using HttpMqttTopics = SubscriptionMap;

/**
 * @brief Response envelope shared by all HTTP MQTT operation handlers.
 */
struct HttpMqttResult {
    int statusCode{0};                                  ///< HTTP status code.
    HttpMqttHeaders headers{};                          ///< Response headers.
    std::string payload{};                              ///< Response payload body.
    std::optional<std::uint8_t> present{};              ///< Optional connect present flag.
    std::optional<std::string> token{};                 ///< Optional token payload.
    std::optional<double> runtime{};                    ///< Optional runtime metric.
    std::optional<std::uint16_t> packetId{};            ///< Optional packet identifier.
};

/**
 * @brief Callback signature used to validate one HTTP MQTT response.
 */
using HttpMqttResultCheck = std::function<void(const HttpMqttResult&)>;

/**
 * @brief Request envelope returned by HTTP MQTT request builders.
 */
struct HttpMqttRequestData {
    HttpMqttHeaders headers{};              ///< Request headers.
    std::string payload{};                  ///< Serialized request payload.
    HttpMqttResultCheck resultCheck{};      ///< Operation-specific response validator.
};

/**
 * @brief Returns standard JSON request headers for HTTP MQTT operations.
 * @return Header map with content-type, accept, and accept-charset.
 */
[[nodiscard]] HttpMqttHeaders makeStandardJsonHeaders();

/**
 * @brief Returns standard text request headers for HTTP MQTT operations.
 * @return Header map with content-type, accept, and accept-charset.
 */
[[nodiscard]] HttpMqttHeaders makeStandardTextHeaders();

/**
 * @brief Returns one copy of headers with normalized lowercase key names.
 * @param headersInput Input headers.
 * @return Lowercase-key header map.
 */
[[nodiscard]] HttpMqttHeaders normalizeHeaderKeys(const HttpMqttHeaders& headersInput);

/**
 * @brief Returns optional header value for one key lookup.
 * @param headersInput Header map.
 * @param headerName Header name lookup key.
 * @return Header value when present, otherwise nullopt.
 */
[[nodiscard]] std::optional<std::string> tryReadHeaderValue(
    const HttpMqttHeaders& headersInput,
    std::string_view headerName);

/**
 * @brief Returns true when one header value starts with one expected prefix.
 * @param headersInput Header map.
 * @param headerName Header name lookup key.
 * @param valuePrefix Required prefix.
 * @return True when header exists and starts with prefix.
 */
[[nodiscard]] bool headerValueStartsWith(
    const HttpMqttHeaders& headersInput,
    std::string_view headerName,
    std::string_view valuePrefix);

/**
 * @brief Returns one required header value or throws on absence.
 * @param headersInput Header map.
 * @param headerName Required header key.
 * @return Header value.
 * @throws std::runtime_error when required header is missing.
 */
[[nodiscard]] std::string requireHeaderValue(
    const HttpMqttHeaders& headersInput,
    std::string_view headerName);

/**
 * @brief Parses one optional packet id string into uint16.
 * @param packetIdText Packet id text.
 * @return Parsed packet id when valid, otherwise nullopt.
 */
[[nodiscard]] std::optional<std::uint16_t> parsePacketId(std::string_view packetIdText);

/**
 * @brief Reads and parses packet id from one header map.
 * @param headersInput Header map.
 * @return Parsed packet id when present and valid, otherwise nullopt.
 */
[[nodiscard]] std::optional<std::uint16_t> readPacketIdHeader(const HttpMqttHeaders& headersInput);

/**
 * @brief Resolves version from headers with fallback default.
 * @param headersInput Header map.
 * @param defaultVersion Fallback version string.
 * @return Header version when present and non-empty, otherwise default version.
 */
[[nodiscard]] std::string resolveVersion(
    const HttpMqttHeaders& headersInput,
    std::string_view defaultVersion);

/**
 * @brief Validates that one payload contains a JSON object.
 * @param payloadText Payload text.
 * @param contextText Operation context used in exception messages.
 * @throws std::runtime_error when payload is not a JSON object.
 */
void requireJsonObjectPayload(std::string_view payloadText, std::string_view contextText);

} // namespace yaha
