#pragma once

/**
 * @file opensensemap_client_app.h
 * @brief Runtime config mapping and HTTP sender factory for OpenSenseMap client.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/opensensemap/opensensemap_component.h"

#include <functional>
#include <string>
#include <utility>

namespace yaha {

/**
 * @brief Runtime config model for standalone OpenSenseMap client process.
 */
struct OpenSenseMapClientRuntimeConfig {
    OpenSenseMapConfig openSenseMapConfig{};   ///< OpenSenseMap domain config.
    YahaMqttClient::Config mqttConfig{};       ///< Generic MQTT runtime config.
    bool logIncomingMessages{false};           ///< Enable inbound MQTT message logs in broker-format.
};

/**
 * @brief Loads OpenSenseMap domain config from INI document.
 * @param document Parsed INI document.
 * @param output Output config model.
 * @param errorMessage Error text on failure.
 * @return True when parsing succeeded.
 */
[[nodiscard]] bool tryLoadOpenSenseMapConfigFromIni(
    const IniDocument& document,
    OpenSenseMapConfig& output,
    std::string& errorMessage);

/**
 * @brief Loads full runtime config from INI document.
 * @param document Parsed INI document.
 * @param output Output runtime config.
 * @param errorMessage Error text on failure.
 * @return True when parsing succeeded.
 */
[[nodiscard]] bool tryLoadOpenSenseMapClientRuntimeConfigFromIni(
    const IniDocument& document,
    OpenSenseMapClientRuntimeConfig& output,
    std::string& errorMessage);

/**
 * @brief Builds default OpenSenseMap HTTP sender from runtime config.
 * @param config OpenSenseMap runtime config.
 * @return Request sender callback.
 */
[[nodiscard]] OpenSenseMapRequestSender makeOpenSenseMapRequestSender(
    const OpenSenseMapConfig& config);

/**
 * @brief Builds OpenSenseMap HTTP sender with injectable command executor.
 * @param config OpenSenseMap runtime config.
 * @param commandExecutor Command runner used to execute curl command text.
 * @return Request sender callback.
 */
[[nodiscard]] OpenSenseMapRequestSender makeOpenSenseMapRequestSender(
    const OpenSenseMapConfig& config,
    std::function<std::pair<int, std::string>(const std::string&)> commandExecutor);

} // namespace yaha
