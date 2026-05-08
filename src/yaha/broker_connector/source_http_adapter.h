#pragma once

/**
 * @file source_http_adapter.h
 * @brief Source HTTP MQTT 1.0 adapter for Broker Connector Phase 2.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace yaha {

/**
 * @brief Source broker connectivity configuration.
 */
struct SourceHttpBrokerConfig {
    static constexpr std::uint16_t k_default_broker_port{1883U};
    static constexpr std::uint32_t k_default_keep_alive_seconds{30U};

    std::string brokerHost{"127.0.0.1"};
    std::uint16_t brokerPort{k_default_broker_port};
    std::string clientId{"broker-connector-source"};
    bool clean{true};
    std::uint32_t keepAliveSeconds{k_default_keep_alive_seconds};
    std::string listenerHost{"127.0.0.1"};
    std::string listenerBindHost{"127.0.0.1"};
    std::uint16_t listenerPort{0U};
    SubscriptionMap subscribeTopics{};
};

/**
 * @brief Source metadata for one inbound publish callback.
 */
struct SourcePublishMeta {
    Qos qos{Qos::AtLeastOnce};
    bool retain{false};
    bool dup{false};
    std::optional<std::uint16_t> packetId{};
};

/**
 * @brief Callback type for source inbound publish data.
 */
using SourcePublishCallback = std::function<void(const Message&, const SourcePublishMeta&)>;

/**
 * @brief Source-side adapter for HTTP MQTT 1.0 connect/subscribe/ping/disconnect and callbacks.
 */
class SourceHttpBrokerAdapter {
public:
    /**
     * @brief Constructs source adapter from source broker configuration.
     * @param config Source broker configuration.
     */
    explicit SourceHttpBrokerAdapter(SourceHttpBrokerConfig config);

    /**
     * @brief Destructor that closes adapter state.
     */
    ~SourceHttpBrokerAdapter();

    SourceHttpBrokerAdapter(const SourceHttpBrokerAdapter&) = delete;
    SourceHttpBrokerAdapter& operator=(const SourceHttpBrokerAdapter&) = delete;

    /**
     * @brief Sets callback invoked for every inbound source publish.
     * @param callback Inbound publish callback.
     */
    void setIncomingPublishCallback(SourcePublishCallback callback);

    /**
     * @brief Starts listener (if needed), connects to source, and subscribes configured topics.
     * @param errorMessage Human-readable error text on failure.
     * @return True on success.
     */
    [[nodiscard]] bool connectAndSubscribe(std::string& errorMessage);

    /**
     * @brief Sends one keep-alive ping to source broker.
     * @param errorMessage Human-readable error text on failure.
     * @return True on success.
     */
    [[nodiscard]] bool ping(std::string& errorMessage);

    /**
     * @brief Closes source session and callback listener.
     */
    void close();

    /**
     * @brief Returns current source connection state.
     * @return True when source session is connected.
     */
    [[nodiscard]] bool isConnected() const;

    /**
     * @brief Returns effective bound callback listener port.
     * @return Listener port, or 0 when not bound.
     */
    [[nodiscard]] std::uint16_t listenerPort() const;

private:
    bool startListener(std::string& errorMessage);
    void stopListener();

    bool sendConnect(std::string& errorMessage, std::string& responseSummary);
    bool sendSubscribe(std::string& errorMessage, std::string& responseSummary);
    bool sendDisconnect() const;

    [[nodiscard]] static std::string buildConnectPayload(const SourceHttpBrokerConfig& config,
                                                         std::uint16_t effectiveListenerPort);
    [[nodiscard]] static std::string buildSubscribePayload(const SourceHttpBrokerConfig& config);

    SourceHttpBrokerConfig config_;

    mutable std::mutex stateMutex_{};
    bool connected_{false};
    std::string sendToken_{};
    std::string receiveToken_{};
    std::uint16_t boundListenerPort_{0U};
    std::uint16_t nextPacketId_{1U};
    SourcePublishCallback publishCallback_{};

    std::unique_ptr<httplib::Server> server_{};
    std::thread listenerThread_{};
};

} // namespace yaha
