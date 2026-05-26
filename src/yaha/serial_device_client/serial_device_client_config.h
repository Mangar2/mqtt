#pragma once

/**
 * @file serial_device_client_config.h
 * @brief Runtime config mapping helpers for YAHA SerialDevice standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/serial_device/serial_device_contract.h"

#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for SerialDevice standalone process.
 */
struct SerialDeviceClientRuntimeConfig {
    SerialDeviceConfig serialDeviceConfig{};
    YahaMqttClient::Config mqttConfig{};
};

/**
 * @brief Loads SerialDevice domain config from INI document.
 * @param document Parsed INI document.
 * @param output Domain config output.
 * @param errorMessage Parse error text on failure.
 * @return True on success.
 */
[[nodiscard]] bool tryLoadSerialDeviceConfigFromIni(
    const IniDocument& document,
    SerialDeviceConfig& output,
    std::string& errorMessage);

/**
 * @brief Loads full standalone runtime config from INI document.
 * @param document Parsed INI document.
 * @param output Runtime config output.
 * @param errorMessage Parse error text on failure.
 * @return True on success.
 */
[[nodiscard]] bool tryLoadSerialDeviceClientRuntimeConfigFromIni(
    const IniDocument& document,
    SerialDeviceClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
