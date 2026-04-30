#pragma once

/**
 * @file mqtt_client_config.h
 * @brief Shared INI mapping helpers for MQTT client config and subscriptions.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <string>
#include <string_view>

namespace yaha {

/**
 * @brief Maps optional [mqtt] INI values into YahaMqttClient::Config.
 * @param document Parsed INI document.
 * @param output MQTT config to fill.
 * @param errorMessage Human-readable parser error text on invalid value.
 * @return True when values are valid.
 */
[[nodiscard]] bool tryLoadMqttClientConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage);

/**
 * @brief Parses subscription map from one INI section.
 * @param document Parsed INI document.
 * @param sectionName Section containing topic=qos entries.
 * @param output Subscription map output.
 * @param errorMessage Human-readable parser error text on invalid value.
 * @return True when values are valid.
 */
[[nodiscard]] bool tryLoadSubscriptionsFromIni(
    const IniDocument& document,
    std::string_view sectionName,
    SubscriptionMap& output,
    std::string& errorMessage);

} // namespace yaha
