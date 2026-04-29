#pragma once

/**
 * @file mqtt_component.h
 * @brief IMqttComponent interface for YAHA MQTT-facing components.
 */

#include "yaha/message/message.h"

#include <functional>
#include <map>
#include <string>

namespace yaha {

/**
 * @brief Mapping of MQTT topic filters to requested QoS level.
 */
using SubscriptionMap = std::map<std::string, Qos>;

/**
 * @brief Callback signature for outgoing publishes from a component.
 */
using PublishCallback = std::function<void(const Message&)>;

/**
 * @brief Abstract boundary between a YAHA component and MQTT transport.
 */
class IMqttComponent {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IMqttComponent();

    /**
     * @brief Returns topic filters this component wants to subscribe to.
     * @return Topic-filter to QoS map.
     */
    [[nodiscard]] virtual SubscriptionMap getSubscriptions() const = 0;

    /**
     * @brief Handles one incoming message delivered by the MQTT client.
     * @param message Incoming message.
     */
    virtual void handleMessage(const Message& message) = 0;

    /**
     * @brief Injects publish callback used for outgoing messages.
     *
     * Components that do not publish may ignore this callback.
     *
     * @param callback Publish callback to broker transport.
     */
    virtual void setPublishCallback(PublishCallback callback);

protected:
    /**
     * @brief Protected default constructor.
     */
    IMqttComponent() = default;

    /**
     * @brief Copy constructor.
     * @param other Source object.
     */
    IMqttComponent(const IMqttComponent& other) = default;

    /**
     * @brief Copy assignment operator.
     * @param other Source object.
     * @return Reference to this object.
     */
    IMqttComponent& operator=(const IMqttComponent& other) = default;

    /**
     * @brief Move constructor.
     * @param other Source object.
     */
    IMqttComponent(IMqttComponent&& other) = default;

    /**
     * @brief Move assignment operator.
     * @param other Source object.
     * @return Reference to this object.
     */
    IMqttComponent& operator=(IMqttComponent&& other) = default;
};

} // namespace yaha
