#pragma once

/**
 * @file value_service_client_app.h
 * @brief Runtime config types and mapping helpers for YAHA ValueService standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/value_service/value_service_component.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for the ValueService standalone process.
 */
struct ValueServiceClientRuntimeConfig {
    ValueServiceConfig valueServiceConfig{}; ///< ValueService domain configuration.
    YahaMqttClient::Config mqttConfig{};     ///< MQTT runtime configuration.
};

/**
 * @brief Maps ValueService domain config from parsed INI document.
 * @param document Parsed INI document.
 * @param output ValueService config output.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadValueServiceConfigFromIni(
    const IniDocument& document,
    ValueServiceConfig& output,
    std::string& errorMessage);

/**
 * @brief Maps runtime configuration from parsed INI document.
 * @param document Parsed INI document.
 * @param output Loaded runtime configuration on success.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadValueServiceClientRuntimeConfigFromIni(
    const IniDocument& document,
    ValueServiceClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
