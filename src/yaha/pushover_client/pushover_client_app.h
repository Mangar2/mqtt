#pragma once

/**
 * @file pushover_client_app.h
 * @brief Runtime config mapping and sender factory for Pushover client.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/pushover/pushover_component.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime config model for standalone Pushover client process.
 */
struct PushoverClientRuntimeConfig {
    PushoverConfig pushoverConfig{};       ///< Pushover domain config.
    YahaMqttClient::Config mqttConfig{};   ///< Generic MQTT runtime config.
};

/**
 * @brief Loads Pushover domain config from INI document.
 * @param document Parsed INI document.
 * @param output Output Pushover config.
 * @param errorMessage Error text on failure.
 * @return True when parsing succeeded.
 */
[[nodiscard]] bool tryLoadPushoverConfigFromIni(
    const IniDocument& document,
    PushoverConfig& output,
    std::string& errorMessage);

/**
 * @brief Loads full runtime config from INI document.
 * @param document Parsed INI document.
 * @param output Output runtime config.
 * @param errorMessage Error text on failure.
 * @return True when parsing succeeded.
 */
[[nodiscard]] bool tryLoadPushoverClientRuntimeConfigFromIni(
    const IniDocument& document,
    PushoverClientRuntimeConfig& output,
    std::string& errorMessage);

/**
 * @brief Builds default Pushover HTTP sender from runtime config.
 * @param config Runtime config.
 * @return Sender callback.
 */
[[nodiscard]] PushoverRequestSender makePushoverRequestSender(
    const PushoverConfig& config);

} // namespace yaha
