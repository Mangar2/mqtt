#pragma once

/**
 * @file broker_connector_client_app.h
 * @brief Runtime config types and INI mapping helpers for YAHA Broker Connector standalone process.
 */

#include "yaha/broker_connector/relay_component.h"
#include "yaha/broker_connector/source_http_adapter.h"
#include "yaha/broker_connector/source_lifecycle_manager.h"
#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <chrono>
#include <string>

namespace yaha {

/**
 * @brief Runtime configuration for Broker Connector standalone process.
 */
struct BrokerConnectorClientRuntimeConfig {
    SourceHttpBrokerConfig sourceConfig{};             ///< Source HTTP broker configuration.
    SourceLifecycleConfig sourceLifecycleConfig{};     ///< Source lifecycle timing configuration.
    YahaMqttClient::Config receiverConfig{             ///< Receiver MQTT client configuration.
        .brokerHost = "127.0.0.1",
        .brokerPort = 1883U,
        .clientId = "broker-connector-receiver",
        .reconnectDelay = std::chrono::milliseconds{1000},
        .keepAliveInterval = std::chrono::milliseconds{30000},
        .loopSleep = std::chrono::milliseconds{20},
        .enableLifecycleTrace = true,
        .enableMessageTrace = false};
    RelayPolicyConfig relayPolicyConfig{};             ///< Relay policy and retry configuration.
};

/**
 * @brief Maps source HTTP broker config from parsed INI document.
 * @param document Parsed INI document.
 * @param output Source config output.
 * @param errorMessage Human-readable parser error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadSourceHttpBrokerConfigFromIni(
    const IniDocument& document,
    SourceHttpBrokerConfig& output,
    std::string& errorMessage);

/**
 * @brief Maps receiver MQTT broker config from parsed INI document.
 * @param document Parsed INI document.
 * @param output Receiver config output.
 * @param errorMessage Human-readable parser error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadReceiverMqttBrokerConfigFromIni(
    const IniDocument& document,
    YahaMqttClient::Config& output,
    std::string& errorMessage);

/**
 * @brief Maps full Broker Connector runtime config from parsed INI document.
 * @param document Parsed INI document.
 * @param output Runtime config output.
 * @param errorMessage Human-readable parser error text on failure.
 * @return True when parsing and validation succeeded.
 */
[[nodiscard]] bool tryLoadBrokerConnectorClientRuntimeConfigFromIni(
    const IniDocument& document,
    BrokerConnectorClientRuntimeConfig& output,
    std::string& errorMessage);

} // namespace yaha
