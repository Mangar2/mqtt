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
 * @brief Applies per-node device overrides from a JSON settings document.
 * @param jsonText JSON payload loaded from FileStore.
 * @param output ZWave config to update.
 * @param errorMessage Human-readable error text on parse/validation failure.
 * @return True when JSON parsing and device merge succeeded.
 */
[[nodiscard]] bool tryApplyZwaveDeviceSettingsFromJson(
    const std::string& jsonText,
    ZwaveConfig& output,
    std::string& errorMessage);

/**
 * @brief Serializes only ZWave device rows to JSON for FileStore persistence.
 * @param config Effective ZWave settings.
 * @return JSON string containing only the root `devices` array.
 */
[[nodiscard]] std::string serializeZwaveSettingsToJson(const ZwaveConfig& config);

/**
 * @brief Loads device overrides from FileStore and persists the merged snapshot.
 * @param config ZWave configuration to update in-place.
 * @param errorMessage Human-readable error text on failure.
 * @return True when load/merge/persist succeeded.
 */
[[nodiscard]] bool trySyncZwaveDeviceSettingsFromFileStore(
    ZwaveConfig& config,
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
