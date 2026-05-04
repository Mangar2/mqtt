#pragma once

/**
 * @file automation_client_app.h
 * @brief Runtime config mapping helpers for YAHA Automation standalone process.
 */

#include "yaha/automation_client/automation_client_component.h"
#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime config for standalone automation client process.
 */
struct AutomationClientRuntimeConfig {
    AutomationClientConfig automationConfig{}; ///< Automation component settings.
    YahaMqttClient::Config mqttConfig{};       ///< MQTT runtime settings.
};

/**
 * @brief Loads automation domain config from INI document.
 * @param document Parsed INI document.
 * @param output Parsed automation config.
 * @param errorMessage Human-readable error text when loading fails.
 * @return True on successful parsing.
 */
[[nodiscard]] bool tryLoadAutomationClientConfigFromIni(
    const IniDocument& document,
    AutomationClientConfig& output,
    std::string& errorMessage);

/**
 * @brief Loads full runtime config from INI document.
 * @param document Parsed INI document.
 * @param output Runtime config output.
 * @param errorMessage Human-readable error text when loading fails.
 * @return True on successful parsing.
 */
[[nodiscard]] bool tryLoadAutomationClientRuntimeConfigFromIni(
    const IniDocument& document,
    AutomationClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
