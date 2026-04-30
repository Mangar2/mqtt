#pragma once

/**
 * @file message_store_client_app.h
 * @brief Composition root for YAHA MessageStore standalone process.
 */

#include "yaha/message_store/message_store.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <filesystem>
#include <optional>
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
 * @brief Standalone process composition for MessageStore + YahaMqttClient.
 */
class MessageStoreClientApp {
public:
    enum class ConnectionEvent {
        Connected,
        Disconnected,
    };

    /**
     * @brief Constructs the app from runtime configuration.
     * @param config Runtime configuration.
     */
    explicit MessageStoreClientApp(MessageStoreClientRuntimeConfig config);

    /**
     * @brief Starts MessageStore and MQTT session loops.
     */
    void run();

    /**
     * @brief Stops MQTT session and MessageStore lifecycle.
     */
    void close();

    /**
     * @brief Returns whether both components are currently running.
     * @return True when app is running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns whether MQTT transport is currently connected to broker.
     * @return True when MQTT connection is established.
     */
    [[nodiscard]] bool isConnected() const;

    /**
     * @brief Returns one MQTT connection transition event when state changed.
     * @return Connected/Disconnected when state toggled since last poll, otherwise nullopt.
     */
    [[nodiscard]] std::optional<ConnectionEvent> pollConnectionEvent();

    /**
     * @brief Loads runtime configuration from an INI-like text file.
     * @param configPath Path to configuration file.
     * @param output Loaded runtime configuration on success.
     * @param errorMessage Human-readable error text on failure.
     * @return True when parsing and validation succeeded.
     */
    [[nodiscard]] static bool tryLoadConfigFromFile(
        const std::filesystem::path& configPath,
        MessageStoreClientRuntimeConfig& output,
        std::string& errorMessage);

private:
    [[nodiscard]] static YahaMqttClient::Transport makeBrokerTransport();

    MessageStore configStore_;
    YahaMqttClient mqttClient_;
    std::optional<bool> lastObservedConnectionState_{};
};

} // namespace yaha
