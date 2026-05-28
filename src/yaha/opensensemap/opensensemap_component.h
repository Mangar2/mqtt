#pragma once

/**
 * @file opensensemap_component.h
 * @brief OpenSenseMap MQTT component that forwards configured sensor values via HTTP.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

inline constexpr std::uint16_t kDefaultOpenSenseMapPort = 443U;

/**
 * @brief One configured OpenSenseMap sensor mapping.
 */
struct OpenSenseMapSensorConfig {
    std::string sensorName{};        ///< Human-readable sensor name.
    std::string sensorUnit{};        ///< Sensor unit string.
    std::string topicFilter{};       ///< MQTT topic mapped to this sensor.
    std::string sensorIdentifier{};  ///< OpenSenseMap sensor identifier.
};

/**
 * @brief HTTP response model returned by OpenSenseMap sender callback.
 */
struct OpenSenseMapHttpResult {
    int statusCode{0};               ///< HTTP status code.
    std::string payload{};           ///< Raw HTTP response payload.
    std::string contentType{};       ///< HTTP response content type header value.
};

/**
 * @brief Runtime configuration for OpenSenseMap component.
 */
struct OpenSenseMapConfig {
    std::string stationName{};                                   ///< Optional station name.
    std::string boxIdentifier{};                                 ///< OpenSenseMap sense box identifier.
    std::string host{"ingress.opensensemap.org"};              ///< OpenSenseMap host.
    std::uint16_t port{kDefaultOpenSenseMapPort};                ///< OpenSenseMap port.
    bool useTls{true};                                           ///< Enables HTTPS sender mode.
    Qos subscribeQos{Qos::AtLeastOnce};                          ///< Subscription QoS for sensor topics.
    std::vector<OpenSenseMapSensorConfig> sensors{};             ///< Configured sensor mappings.
};

/**
 * @brief Callback used by component to execute one HTTP publish request.
 * @param requestPath Path component for POST request.
 * @param requestPayload JSON payload.
 * @return HTTP result.
 */
using OpenSenseMapRequestSender =
    std::function<OpenSenseMapHttpResult(const std::string& requestPath, const std::string& requestPayload)>;

/**
 * @brief IMqttComponent implementation for OpenSenseMap forwarding.
 */
class OpenSenseMapComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component with config and sender callback.
     * @param config Runtime configuration.
     * @param requestSender HTTP sender callback.
     */
    OpenSenseMapComponent(OpenSenseMapConfig config, OpenSenseMapRequestSender requestSender);

    /**
     * @brief Returns MQTT subscriptions for all configured sensor topics.
     * @return Topic to QoS map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one inbound MQTT message and forwards mapped values.
     * @param message Inbound MQTT message.
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
     * @brief Stores callback used for outgoing MQTT status publishes.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

private:
    [[nodiscard]] std::optional<OpenSenseMapSensorConfig> findSensorForTopic(const std::string& topicName) const;
    [[nodiscard]] static std::optional<double> toNumericValue(const Value& valueVariant);
    [[nodiscard]] static std::string makeRequestPayload(double numericValue);
    [[nodiscard]] static std::string makeRequestPath(
        const std::string& boxIdentifier,
        const std::string& sensorIdentifier);
    [[nodiscard]] static std::string buildResultReason(
        const std::string& topicName,
        double numericValue,
        const OpenSenseMapHttpResult& result);
    [[nodiscard]] static std::string extractJsonMessage(const std::string& payloadText);
    [[nodiscard]] static Message buildStatusMessage(
        int statusCode,
        const Message& sourceMessage,
        const std::string& resultReason);
    void publishStatusMessage(const Message& statusMessage) const;

    OpenSenseMapConfig config_{};
    OpenSenseMapRequestSender requestSender_{};

    mutable std::mutex stateMutex_{};
    bool running_{false};

    mutable std::mutex publishMutex_{};
    PublishCallback publishCallback_{};
};

} // namespace yaha
