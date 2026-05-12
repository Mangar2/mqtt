#pragma once

/**
 * @file mqtt_client.h
 * @brief YahaMqttClient session driver for IMqttComponent implementations.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace yaha {

/**
 * @brief Reusable MQTT session driver for a single IMqttComponent instance.
 */
class YahaMqttClient {
public:
    static constexpr std::uint16_t k_default_broker_port{1883U};
    static constexpr std::int64_t k_default_reconnect_delay_ms{1000};
    static constexpr std::int64_t k_default_keep_alive_interval_ms{30000};
    static constexpr std::int64_t k_default_loop_sleep_ms{20};

    /**
     * @brief Runtime configuration for one client session.
     */
    struct Config {
        std::string brokerHost{"127.0.0.1"};
        std::uint16_t brokerPort{k_default_broker_port};
        std::string clientId{"yaha-client"};
        std::chrono::milliseconds reconnectDelay{k_default_reconnect_delay_ms};
        std::chrono::milliseconds keepAliveInterval{k_default_keep_alive_interval_ms};
        std::chrono::milliseconds loopSleep{k_default_loop_sleep_ms};
        bool enableLifecycleTrace{true};
        bool enableMessageTrace{false};
        bool logReason{true};
    };

    /**
     * @brief Transport callback bundle consumed by YahaMqttClient.
     */
    struct Transport {
        std::function<bool(const Config&)> connect;
        std::function<void()> disconnect;
        std::function<void(const Message&)> publish;
        std::function<void(const std::string&, Qos)> subscribe;
        std::function<void(const std::string&)> unsubscribe;
        std::function<std::optional<Message>()> pollIncoming;
        std::function<void()> ping;
        std::function<bool()> isConnected;
    };

    /**
     * @brief Constructs the client with configuration, component, and transport callbacks.
     * @param config Session configuration.
     * @param component Wired component that receives incoming messages.
     * @param transport Transport callback bundle.
     */
    YahaMqttClient(Config config, IMqttComponent& component, Transport transport);

    /**
     * @brief Virtual destructor that stops the worker thread if still running.
     */
    ~YahaMqttClient();

    YahaMqttClient(const YahaMqttClient&) = delete;
    YahaMqttClient& operator=(const YahaMqttClient&) = delete;

    /**
     * @brief Starts the background session loop.
     */
    void run();

    /**
     * @brief Stops the background session loop and disconnects transport.
     */
    void close();

    /**
     * @brief Publishes one message through transport after validation.
     * @param message Message to publish.
     */
    void publish(const Message& message);

    /**
     * @brief Returns whether the worker loop is currently running.
     * @return True while running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns current connection state tracked by the loop.
     * @return True when connected.
     */
    [[nodiscard]] bool isConnected() const;

private:
    void workerLoop();
    bool ensureConnected();
    void replaySubscriptions();
    void resyncSubscriptions();
    void unsubscribeAll();
    void processIncoming();
    void processKeepAlive();
    void traceLifecycle(const std::string& text) const;
    void traceMessage(const std::string& direction, const Message& message) const;
    [[nodiscard]] bool isTopicSubscribed(const std::string& topic) const;
    [[nodiscard]] static bool topicMatchesFilter(const std::string& filter,
                                                 const std::string& topic);

    Config config_;
    IMqttComponent& component_;
    Transport transport_;

    mutable std::mutex stateMutex_;
    bool running_{false};
    bool connected_{false};
    bool everConnected_{false};
    SubscriptionMap activeSubscriptions_;
    std::chrono::steady_clock::time_point lastPingAt_{};

    std::thread workerThread_;
};

} // namespace yaha
