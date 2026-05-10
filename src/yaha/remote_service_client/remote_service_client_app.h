#pragma once

/**
 * @file remote_service_client_app.h
 * @brief Runtime config types and mapping helpers for YAHA RemoteService standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/remote_service/remote_service_config.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for the RemoteService standalone process.
 */
struct RemoteServiceClientRuntimeConfig {
    RemoteServiceConfig remoteServiceConfig{}; ///< RemoteService domain configuration.
    YahaMqttClient::Config mqttConfig{};       ///< MQTT runtime configuration.
};

/**
 * @brief Maps RemoteService domain config from parsed INI document.
 * @param document Parsed INI document.
 * @param output RemoteService config output.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadRemoteServiceConfigFromIni(
    const IniDocument& document,
    RemoteServiceConfig& output,
    std::string& errorMessage);

/**
 * @brief Maps runtime configuration from parsed INI document.
 * @param document Parsed INI document.
 * @param output Loaded runtime configuration on success.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadRemoteServiceClientRuntimeConfigFromIni(
    const IniDocument& document,
    RemoteServiceClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha