#pragma once

/**
 * @file message_store_client_app.h
 * @brief Runtime config types and mapping helpers for YAHA MessageStore standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/message_store/message_store.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for the MessageStore standalone process.
 */
struct MessageStoreClientRuntimeConfig {
    MessageStoreConfig storeConfig{};             ///< MessageStore component configuration.
    YahaMqttClient::Config mqttConfig{};          ///< MQTT session configuration.
};

/**
 * @brief Maps MessageStore domain config from parsed INI document.
 * @param document Parsed INI document.
 * @param output MessageStore config output.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadMessageStoreConfigFromIni(
    const IniDocument& document,
    MessageStoreConfig& output,
    std::string& errorMessage);

/**
 * @brief Maps runtime configuration from parsed INI document.
 * @param document Parsed INI document.
 * @param output Loaded runtime configuration on success.
 * @param errorMessage Human-readable error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadMessageStoreClientRuntimeConfigFromIni(
    const IniDocument& document,
    MessageStoreClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
