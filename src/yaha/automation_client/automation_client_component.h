#pragma once

/**
 * @file automation_client_component.h
 * @brief Automation client IMqttComponent with FileStore-backed rule lifecycle.
 */

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace yaha {

constexpr std::uint16_t k_default_file_store_port{8210U};

/**
 * @brief Runtime config for Automation client component.
 */
struct AutomationClientConfig {
    std::string rulesKeyPath{"/automation/rules"};                    ///< FileStore key path for full rules tree.
    std::string fileStoreHost{"127.0.0.1"};                           ///< FileStore HTTP host.
    std::uint16_t fileStorePort{k_default_file_store_port};            ///< FileStore HTTP port.
    bool fileStoreEnabled{true};                                        ///< Enables startup read and write-back.
    std::string monitorTopicPrefix{"$MONITOR/FileStore"};             ///< Prefix for FileStore monitoring topics.
    std::string managementTopicPrefix{"$MONITORING/automation/rules"}; ///< Prefix for runtime rule update topics.
    Qos subscribeQos{Qos::AtLeastOnce};                                ///< Requested QoS for subscriptions.
};

/**
 * @brief Automation client component handling rule loading and updates.
 */
class AutomationClientComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component from runtime configuration.
     * @param config Runtime configuration.
     */
    explicit AutomationClientComponent(AutomationClientConfig config);

    /**
     * @brief Returns subscriptions for monitoring and management channels.
     * @return Topic filter map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one incoming MQTT message.
     * @param message Incoming message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts lifecycle and initial FileStore rule load.
     */
    void run() override;

    /**
     * @brief Stops lifecycle.
     */
    void close() override;

    /**
     * @brief Stores publish callback for outgoing acknowledgments.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Returns whether lifecycle is running.
     * @return True when running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns current loaded rule count for tests and diagnostics.
     * @return Number of rules in `rules` object.
     */
    [[nodiscard]] std::size_t ruleCount() const;

    /**
     * @brief Checks whether one rule name exists.
     * @param ruleName Rule key under `rules` object.
     * @return True when rule exists.
     */
    [[nodiscard]] bool hasRule(const std::string& ruleName) const;

private:
    [[nodiscard]] bool loadRulesFromFileStore();
    [[nodiscard]] bool persistRulesToFileStore() const;

    void handleMonitoringMessage(const Message& message);
    void handleManagementMessage(const Message& message);

    [[nodiscard]] bool isMonitoringTopic(const std::string& topicName) const;
    [[nodiscard]] bool isManagementTopic(const std::string& topicName) const;

    [[nodiscard]] std::optional<std::string> extractRuleNameFromManagementTopic(
        const std::string& topicName) const;

    [[nodiscard]] static std::optional<std::string> extractMonitoringKeyPath(
        const std::string& payload);

    [[nodiscard]] static std::optional<std::string> extractMonitoringChangeType(
        const std::string& payload);

    [[nodiscard]] static std::optional<RuleTreeNode> parseJsonNode(const std::string& payload);
    [[nodiscard]] static std::string toJsonText(const RuleTreeNode& node);

    void publishManagementAck(const std::string& ruleName, const std::string& payloadText) const;

    AutomationClientConfig config_{};

    mutable std::mutex stateMutex_;
    RuleTreeNode rulesRoot_{RuleTreeNode::Object{}};
    bool running_{false};

    mutable std::mutex publishMutex_;
    PublishCallback publishCallback_;
};

} // namespace yaha
