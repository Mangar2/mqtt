#pragma once

/**
 * @file http_mqtt_interface_client_app.h
 * @brief Runtime configuration and server entry point for the standalone HTTP MQTT interface client.
 */

#include "yaha/ini/ini_document.h"

#include <cstdint>
#include <string>

namespace yaha {

constexpr std::uint16_t k_defaultHttpMqttInterfaceListenerPort{8092U};

/**
 * @brief Runtime settings for the standalone HTTP MQTT interface client.
 */
struct HttpMqttInterfaceClientConfig {
    std::string listenerHost{"127.0.0.1"};  ///< HTTP bind host.
    std::uint16_t listenerPort{k_defaultHttpMqttInterfaceListenerPort};  ///< HTTP bind port.
    bool enablePublishPhpAlias{true};         ///< Enables POST /publish.php compatibility alias.
    bool useLegacyPhpResponse{false};         ///< Enables legacy 200 JSON-string response mode.
};

/**
 * @brief Loads client runtime config from INI document.
 * @param iniDocument Parsed INI data.
 * @param configOutput Target config output.
 * @param errorOutput Error text output on failure.
 * @return True when mapping succeeded, otherwise false.
 */
[[nodiscard]] bool tryLoadHttpMqttInterfaceClientConfigFromIni(
    const IniDocument& iniDocument,
    HttpMqttInterfaceClientConfig& configOutput,
    std::string& errorOutput);

/**
 * @brief Runs standalone HTTP MQTT interface server.
 * @param configInput Runtime configuration.
 * @return Exit code (0 success, non-zero failure).
 */
[[nodiscard]] int runHttpMqttInterfaceClient(const HttpMqttInterfaceClientConfig& configInput);

} // namespace yaha
