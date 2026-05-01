#pragma once

/**
 * @file relay_component.h
 * @brief Relay component for Broker Connector Phase 3 source-to-receiver forwarding.
 */

#include "yaha/broker_connector/source_http_adapter.h"
#include "yaha/broker_connector/source_lifecycle_manager.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <memory>

namespace yaha {

/**
 * @brief Runtime policy settings for relay behavior.
 */
struct RelayPolicyConfig {
    std::uint32_t maxPublishRetries{3U};                   ///< Retry attempts after first failed publish.
    std::chrono::milliseconds publishRetryBackoff{100};    ///< Sleep between publish retries.
    bool normalizeQosToAtLeastOnce{true};                  ///< Map source qos 1/2 to outgoing qos 1.
    bool retainPassthrough{true};                          ///< Preserve source retain flag when true.
};

/**
 * @brief Runtime counters for forwarded relay messages.
 */
struct RelayCounters {
    std::uint64_t received{0U};                            ///< Number of accepted source messages.
    std::uint64_t forwarded{0U};                           ///< Number of messages forwarded successfully.
    std::uint64_t failed{0U};                              ///< Number of messages failed after retries.
};

/**
 * @brief Broker connector domain component that relays source messages to receiver publish port.
 */
class BrokerConnectorComponent : public IMqttComponent {
public:
    /**
     * @brief Constructs relay component from policy config.
     * @param config Relay policy configuration.
     */
    explicit BrokerConnectorComponent(RelayPolicyConfig config);

    /**
     * @brief Destructor that closes the component.
     */
    ~BrokerConnectorComponent();

    BrokerConnectorComponent(const BrokerConnectorComponent&) = delete;
    BrokerConnectorComponent& operator=(const BrokerConnectorComponent&) = delete;

    /**
     * @brief Wires source adapter and lifecycle settings into the component.
     * @param sourceAdapter Source HTTP adapter instance.
     * @param lifecycleConfig Source lifecycle loop configuration.
     */
    void setSourceAdapter(SourceHttpBrokerAdapter& sourceAdapter,
                          SourceLifecycleConfig lifecycleConfig);

    /**
     * @brief Returns topic subscriptions for receiver side.
     * @return Empty map because source side drives incoming messages.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles inbound receiver messages.
     * @param message Inbound receiver message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts component runtime state.
     */
    void run() override;

    /**
     * @brief Stops component runtime state.
     */
    void close() override;

    /**
     * @brief Injects publish callback provided by generic mqtt client.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Handles one incoming source publish and forwards it with retry policy.
     * @param message Source message.
     * @param sourceMeta Source transport metadata.
     * @return True when forwarding succeeded.
     */
    [[nodiscard]] bool onIncomingPublish(const Message& message,
                                         const SourcePublishMeta& sourceMeta);

    /**
     * @brief Returns current relay counters snapshot.
     * @return Counter snapshot.
     */
    [[nodiscard]] RelayCounters getStats() const;

    /**
     * @brief Returns whether the component runtime is active.
     * @return True while running.
     */
    [[nodiscard]] bool isRunning() const;

private:
    [[nodiscard]] Message toForwardMessage(const Message& message,
                                           const SourcePublishMeta& sourceMeta) const;

    RelayPolicyConfig config_{};

    mutable std::mutex relay_state_mutex_{};
    PublishCallback publishCallback_{};
    std::unique_ptr<SourceLifecycleManager> sourceLifecycle_{};
    bool running_{false};
    RelayCounters counters_{};
};

} // namespace yaha
