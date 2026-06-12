#pragma once

/**
 * @file mqtt_client_runtime.h
 * @brief Generic process runtime orchestration for YahaMqttClient-based apps.
 */

#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <chrono>

namespace yaha {

/**
 * @brief Generic runtime orchestration around one YahaMqttClient instance.
 */
class YahaMqttClientRuntime {
public:
    static constexpr int k_default_poll_interval_ms{100};

    /**
     * @brief Constructs runtime wrapper around one mqtt client and one component.
     * @param mqttClient Client to run and stop.
     * @param component Component lifecycle to run and close.
     */
    explicit YahaMqttClientRuntime(YahaMqttClient& mqttClient, IMqttComponent& component);

    /**
     * @brief Runs until SIGINT or SIGTERM was received.
     */
    void runUntilSignal();

    /**
     * @brief Requests runtime shutdown without sending an OS signal.
     */
    static void requestShutdown() noexcept;

private:
    YahaMqttClient& mqttClient_;
    IMqttComponent& component_;
    std::chrono::milliseconds pollInterval_{k_default_poll_interval_ms};
};

} // namespace yaha
