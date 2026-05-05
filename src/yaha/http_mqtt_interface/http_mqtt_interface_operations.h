#pragma once

/**
 * @file http_mqtt_interface_operations.h
 * @brief Version 1.0 HTTP MQTT operation handlers and registry wiring.
 */

#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

namespace yaha {

/**
 * @brief Compatibility response mode for browser publish profile.
 */
enum class HttpMqttPublishCompatibilityResponseMode : std::uint8_t {
	Native,     ///< Keep native downstream publish response (typically 204 empty).
	LegacyPhp   ///< Return legacy PHP-compatible 200 response with stringified payload.
};

/**
 * @brief Deployment config for browser publish compatibility profile.
 */
struct HttpMqttPublishCompatibilityConfig {
	bool enablePublishPhpAlias{true};  ///< Enables POST /publish.php alias.
	HttpMqttPublishCompatibilityResponseMode responseMode{HttpMqttPublishCompatibilityResponseMode::Native};
};

/**
 * @brief Compatibility request envelope for publish.php replacement mapping.
 */
struct HttpMqttPublishCompatibilityRequest {
	std::string method{};              ///< HTTP method (POST/PUT).
	std::string endpoint{};            ///< Endpoint path (/publish or /publish.php).
	HttpMqttHeaders headers{};         ///< Incoming headers.
	HttpMqttHeaders fields{};          ///< Form/query key-value fields.
	std::string body{};                ///< Raw request body.
	std::string token{};               ///< Optional publish token to inject into mapped publish call.
};

/**
 * @brief Downstream forwarding callback for mapped publish requests.
 */
using HttpMqttPublishCompatibilityForwarder = std::function<HttpMqttResult(const HttpMqttRequestData&)>;

/**
 * @brief Handles browser publish compatibility mapping and response adaptation.
 * @param interfaces HTTP MQTT interface facade with v1 publish handlers.
 * @param requestInput Compatibility request input.
 * @param configInput Compatibility deployment configuration.
 * @param forwarder Downstream publish forward callback receiving mapped v1 request.
 * @return HTTP-compatible response according to compatibility rules.
 */
[[nodiscard]] HttpMqttResult handlePublishCompatibilityRequest(
	const HttpMqttInterfaces& interfaces,
	const HttpMqttPublishCompatibilityRequest& requestInput,
	const HttpMqttPublishCompatibilityConfig& configInput,
	const HttpMqttPublishCompatibilityForwarder& forwarder);

/**
 * @brief Builds handler registry containing HTTP MQTT version 1.0 operation handlers.
 * @return Handler registry for all request and response operations.
 */
[[nodiscard]] HttpMqttInterfaceHandlerRegistry makeHttpMqttInterfaceHandlerRegistryV1();

/**
 * @brief Builds HTTP MQTT interface facade pre-wired with version 1.0 handlers.
 * @return Interface facade with version 1.0 operation implementations.
 */
[[nodiscard]] HttpMqttInterfaces makeHttpMqttInterfacesV1();

} // namespace yaha
