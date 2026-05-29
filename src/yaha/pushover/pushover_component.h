#pragma once

/**
 * @file pushover_component.h
 * @brief Pushover MQTT component that forwards incidents to the Pushover API.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace yaha {

inline constexpr std::uint16_t kDefaultPushoverPort = 443U;

/**
 * @brief One configured Pushover subscription.
 */
struct PushoverSubscriptionConfig {
    std::string topicFilter{};  ///< MQTT topic filter subscribed by this component.
    Qos qos{Qos::AtLeastOnce};  ///< Requested MQTT QoS for topic filter.
};

/**
 * @brief HTTP response model returned by Pushover sender callback.
 */
struct PushoverHttpResult {
    int statusCode{0};          ///< HTTP response status code.
    std::string payload{};      ///< Raw HTTP response body.
    std::string contentType{};  ///< HTTP response content type header.
};

/**
 * @brief Runtime configuration for the Pushover component.
 */
struct PushoverConfig {
    std::string host{"api.pushover.net"};                   ///< Pushover API host.
    std::string path{"/1/messages.json"};                   ///< Pushover API path.
    std::uint16_t port{kDefaultPushoverPort};                 ///< API port.
    std::string token{};                                      ///< Pushover application token.
    std::string user{};                                       ///< Pushover user key.
    std::vector<std::string> devices{};                       ///< Target device names.
    std::vector<PushoverSubscriptionConfig> subscriptions{};  ///< Inbound subscriptions.
};

/**
 * @brief Callback used by component to execute one Pushover POST request.
 * @param requestPath Path component for HTTP POST request.
 * @param requestPayload JSON request payload.
 * @return HTTP result.
 */
using PushoverRequestSender =
    std::function<PushoverHttpResult(const std::string& requestPath, const std::string& requestPayload)>;

/**
 * @brief IMqttComponent implementation for Pushover forwarding.
 */
class PushoverComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component with runtime config and sender callback.
     * @param config Runtime config.
     * @param requestSender Sender callback.
     */
    PushoverComponent(PushoverConfig config, PushoverRequestSender requestSender);

    /**
     * @brief Returns configured MQTT subscriptions.
     * @return Topic-filter to QoS map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one incoming MQTT message and forwards to Pushover devices.
     * @param message Inbound message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts component lifecycle.
     */
    void run() override;

    /**
     * @brief Stops component lifecycle.
     */
    void close() override;

    /**
     * @brief Stores outgoing publish callback.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

private:
    [[nodiscard]] static std::string formatReasonText(const ReasonList& reasons);
    [[nodiscard]] static std::string valueToText(const Value& valueVariant);
    [[nodiscard]] static int resolvePriority(const Value& valueVariant);
    [[nodiscard]] static std::string buildPayload(
        const std::string& token,
        const std::string& user,
        const std::string& device,
        const std::string& title,
        const std::string& bodyText,
        int priority);
    [[nodiscard]] static std::string buildResultReason(
        int statusCode,
        const std::string& device,
        const std::string& payloadText);
    [[nodiscard]] static Message buildStatusMessage(
        int statusCode,
        const ReasonList& sourceReasons,
        const std::string& resultReason);
    void publishStatusMessage(const Message& statusMessage) const;

    PushoverConfig config_{};
    PushoverRequestSender requestSender_{};

    mutable std::mutex stateMutex_{};
    bool running_{false};

    mutable std::mutex publishMutex_{};
    PublishCallback publishCallback_{};
};

} // namespace yaha
