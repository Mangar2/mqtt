#pragma once

/**
 * @file receiver_publish_port.h
 * @brief Receiver-side publish port contracts and MQTT client adapter for Broker Connector Phase 3.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace yaha {

/**
 * @brief Publish options passed from relay component to receiver publish port.
 */
struct ReceiverPublishOptions {
    Qos qos{Qos::AtLeastOnce};            ///< Effective outgoing qos.
    bool retain{false};                   ///< Effective outgoing retain flag.
    bool dup{false};                      ///< Effective outgoing dup flag.
};

/**
 * @brief Receiver broker runtime configuration mapped to YahaMqttClient.
 */
struct ReceiverMqttBrokerConfig {
    std::string brokerHost{"127.0.0.1"};                     ///< Receiver broker host.
    std::uint16_t brokerPort{1883U};                           ///< Receiver broker port.
    std::string clientId{"broker-connector-receiver"};       ///< Receiver client id.
    std::chrono::milliseconds reconnectDelay{1000};            ///< Reconnect delay after disconnect.
    std::chrono::milliseconds keepAliveInterval{30000};        ///< Ping interval while connected.
    std::chrono::milliseconds loopSleep{20};                   ///< Worker sleep interval.
    bool enableLifecycleTrace{true};                           ///< Enable lifecycle trace output.
    bool enableMessageTrace{false};                            ///< Enable message trace output.
};

/**
 * @brief Receiver-side publish boundary used by BrokerConnectorComponent.
 */
class ReceiverPublishPort {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~ReceiverPublishPort();

    /**
     * @brief Starts receiver publish runtime.
     * @param errorMessage Human-readable failure text if startup fails.
     * @return True when runtime was started.
     */
    [[nodiscard]] virtual bool start(std::string& errorMessage) = 0;

    /**
     * @brief Stops receiver publish runtime.
     */
    virtual void close() = 0;

    /**
     * @brief Publishes one message to receiver broker.
     * @param message Message to publish.
     * @param options Effective publish options for this attempt.
     * @param errorMessage Human-readable failure text on publish failure.
     * @return True when publish was accepted by transport.
     */
    [[nodiscard]] virtual bool publish(const Message& message,
                                       const ReceiverPublishOptions& options,
                                       std::string& errorMessage) = 0;

    /**
     * @brief Returns receiver connection state.
     * @return True when receiver transport is connected.
     */
    [[nodiscard]] virtual bool isConnected() const = 0;

protected:
    /**
     * @brief Protected default constructor.
     */
    ReceiverPublishPort() = default;
};

/**
 * @brief Receiver publish port implementation backed by standard YahaMqttClient.
 */
class ReceiverMqttPublishPort final : public ReceiverPublishPort {
public:
    /**
     * @brief Constructs port with receiver config and default broker transport.
     * @param config Receiver broker runtime configuration.
     */
    explicit ReceiverMqttPublishPort(ReceiverMqttBrokerConfig config);

    /**
     * @brief Constructs port with receiver config and injected transport callbacks.
     * @param config Receiver broker runtime configuration.
     * @param transport Injected transport callbacks for testing or customization.
     */
    ReceiverMqttPublishPort(ReceiverMqttBrokerConfig config,
                            YahaMqttClient::Transport transport);

    /**
     * @brief Destructor that closes runtime.
     */
    ~ReceiverMqttPublishPort() override;

    ReceiverMqttPublishPort(const ReceiverMqttPublishPort&) = delete;
    ReceiverMqttPublishPort& operator=(const ReceiverMqttPublishPort&) = delete;

    /**
     * @brief Starts YahaMqttClient runtime.
     * @param errorMessage Human-readable failure text if startup fails.
     * @return True when runtime was started.
     */
    [[nodiscard]] bool start(std::string& errorMessage) override;

    /**
     * @brief Stops YahaMqttClient runtime.
     */
    void close() override;

    /**
     * @brief Publishes one message through YahaMqttClient.
     * @param message Relay message.
     * @param options Effective publish options.
     * @param errorMessage Human-readable failure text on error.
     * @return True when publish callback completed without exception.
     */
    [[nodiscard]] bool publish(const Message& message,
                               const ReceiverPublishOptions& options,
                               std::string& errorMessage) override;

    /**
     * @brief Returns current receiver connection state.
     * @return True when connected.
     */
    [[nodiscard]] bool isConnected() const override;

private:
    struct Impl;

    static YahaMqttClient::Config toClientConfig(const ReceiverMqttBrokerConfig& config);
    static Message applyPublishOptions(const Message& message,
                                       const ReceiverPublishOptions& options);

    ReceiverMqttBrokerConfig config_{};
    YahaMqttClient::Transport transport_{};

    mutable std::mutex runtime_state_mutex_{};
    std::unique_ptr<Impl> impl_{};
};

} // namespace yaha
