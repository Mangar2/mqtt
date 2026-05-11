#pragma once

/**
 * @file zwave_client_app.h
 * @brief Runtime config types and mapping helpers for YAHA ZWave standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/zwave/zwave_config.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for the ZWave standalone process.
 */
struct ZwaveClientRuntimeConfig {
    ZwaveConfig zwaveConfig{};          ///< ZWave domain configuration.
    YahaMqttClient::Config mqttConfig{}; ///< MQTT runtime configuration.
};

/**
 * @brief Maps ZWave domain config from parsed INI document.
 * @param document Parsed INI document.
 * @param output ZWave config output.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadZwaveConfigFromIni(
    const IniDocument& document,
    ZwaveConfig& output,
    std::string& errorMessage);

/**
 * @brief Maps runtime configuration from parsed INI document.
 * @param document Parsed INI document.
 * @param output Loaded runtime configuration on success.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadZwaveClientRuntimeConfigFromIni(
    const IniDocument& document,
    ZwaveClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
