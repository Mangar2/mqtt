#pragma once

/**
 * @file zwave_service_component.h
 * @brief ZWave IMqttComponent service orchestration with reply matcher flow.
 */

#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/zwave/zwave_config.h"
#include "yaha/zwave_controller/zwave_controller.h"

#include <memory>
#include <string>

namespace yaha {

/**
 * @brief ZWave domain component implementing phase-3 service behavior.
 */
class ZwaveServiceComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs service from domain config and controller.
     * @param config Domain configuration.
     * @param controller Controller adapter instance.
     */
    ZwaveServiceComponent(ZwaveConfig config, std::shared_ptr<IZwaveController> controller);

    /**
     * @brief Applies updated device configuration to controller and emits info publish.
     * @param config Device mapping configuration rows.
     */
    void setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& config);

    /**
     * @brief Returns required subscriptions for management and device set topics.
     * @return Subscription map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one incoming MQTT message.
     * @param message Incoming message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Runs service startup flow.
     */
    void run() override;

    /**
     * @brief Closes service and underlying controller.
     */
    void close() override;

    /**
     * @brief Sets publish callback used for outbound messages.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

private:
    /**
     * @brief Logs one incoming message when incoming tracing is enabled.
     * @param message Message observed on inbound processing path.
     */
    void logIncomingMessageIfEnabled(const Message& message) const;

    /**
     * @brief Logs one outgoing message when outgoing tracing is enabled.
     * @param message Message emitted on outbound processing path.
     */
    void logOutgoingMessageIfEnabled(const Message& message) const;

    /**
     * @brief Handles one publish received from controller.
     * @param message Outbound controller message.
     */
    void handleControllerPublish(const Message& message);

    /**
     * @brief Emits one message through publish callback when configured.
     * @param message Outbound message.
     */
    void publish(const Message& message) const;

    /**
     * @brief Returns true when topic equals remove-failed-node command topic.
     * @param topic Topic text.
     * @return True on remove-failed-node topic.
     */
    [[nodiscard]] static bool isRemoveFailedTopic(const std::string& topic);

    /**
     * @brief Returns true when topic equals add-node command topic.
     * @param topic Topic text.
     * @return True on add-node topic.
     */
    [[nodiscard]] static bool isAddNodeTopic(const std::string& topic);

    /**
     * @brief Returns true when topic equals scan command topic.
     * @param topic Topic text.
     * @return True on scan topic.
     */
    [[nodiscard]] static bool isScanTopic(const std::string& topic);

    ZwaveConfig config_{};
    std::shared_ptr<IZwaveController> controller_{};
    PublishCallback publishCallback_{};
};

} // namespace yaha
