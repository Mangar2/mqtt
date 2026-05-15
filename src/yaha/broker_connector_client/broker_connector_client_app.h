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
#include <optional>
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
        .brokerPort = YahaMqttClient::k_default_broker_port,
        .clientId = "broker-connector-receiver",
        .reconnectDelay = std::chrono::milliseconds{YahaMqttClient::k_default_reconnect_delay_ms},
        .keepAliveInterval = std::chrono::milliseconds{YahaMqttClient::k_default_keep_alive_interval_ms},
        .loopSleep = std::chrono::milliseconds{YahaMqttClient::k_default_loop_sleep_ms},
        .enableLifecycleTrace = true,
        .enableMessageTrace = true};
    RelayPolicyConfig relayPolicyConfig{};             ///< Relay policy and retry configuration.
};

/**
 * @brief Result wrapper for source HTTP broker config parsing.
 */
struct SourceHttpBrokerConfigLoadResult {
    std::optional<SourceHttpBrokerConfig> config{};    ///< Parsed config when successful.
    std::string errorMessage{};                        ///< Human-readable parse error when failed.
};

/**
 * @brief Result wrapper for receiver MQTT broker config parsing.
 */
struct ReceiverMqttBrokerConfigLoadResult {
    std::optional<YahaMqttClient::Config> config{};    ///< Parsed config when successful.
    std::string errorMessage{};                        ///< Human-readable parse error when failed.
};

/**
 * @brief Result wrapper for broker connector runtime config parsing.
 */
struct BrokerConnectorClientRuntimeConfigLoadResult {
    std::optional<BrokerConnectorClientRuntimeConfig> config{};  ///< Parsed config when successful.
    std::string errorMessage{};                                  ///< Human-readable parse error when failed.
};

/**
 * @brief Maps source HTTP broker config from parsed INI document.
 * @param document Parsed INI document.
 * @return Source config result including either parsed value or error text.
 */
[[nodiscard]] SourceHttpBrokerConfigLoadResult tryLoadSourceHttpBrokerConfigFromIni(
    const IniDocument& document);

/**
 * @brief Maps receiver MQTT broker config from parsed INI document.
 * @param document Parsed INI document.
 * @return Receiver config result including either parsed value or error text.
 */
[[nodiscard]] ReceiverMqttBrokerConfigLoadResult tryLoadReceiverMqttBrokerConfigFromIni(
    const IniDocument& document);

/**
 * @brief Maps full Broker Connector runtime config from parsed INI document.
 * @param document Parsed INI document.
 * @return Runtime config result including either parsed value or error text.
 */
[[nodiscard]] BrokerConnectorClientRuntimeConfigLoadResult tryLoadBrokerConnectorClientRuntimeConfigFromIni(
    const IniDocument& document);

} // namespace yaha
