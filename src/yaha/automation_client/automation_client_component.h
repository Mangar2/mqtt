#pragma once

/**
 * @file automation_client_component.h
 * @brief Automation client IMqttComponent with FileStore-backed rule lifecycle.
 */

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation/expression_evaluator.h"
#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace yaha {

constexpr std::uint16_t k_default_file_store_port{8210U};

/**
 * @brief Runtime config for Automation client component.
 */
struct AutomationClientConfig {
    std::string automationTopicPrefix{"$MONITORING/automation"};      ///< Prefix for automation control channels.
    std::string rulesKeyPath{"/automation/rules"};                    ///< FileStore key path for full rules tree.
    std::string fileStoreHost{"127.0.0.1"};                           ///< FileStore HTTP host.
    std::uint16_t fileStorePort{k_default_file_store_port};            ///< FileStore HTTP port.
    bool fileStoreEnabled{true};                                        ///< Enables startup read and write-back.
    std::string monitorTopicPrefix{"$MONITOR/FileStore"};             ///< Prefix for FileStore monitoring topics.
    std::string managementTopicPrefix{"$MONITORING/automation/rules"}; ///< Prefix for runtime rule update topics.
    std::string presenceTopic{"$MONITORING/presence"};                ///< Presence variable topic.
    std::vector<std::string> motionTopics{                              ///< Default motion/control subscriptions.
        "+/+/+/motion sensor/detection state",
        "$MONITORING/presence/set"};
    double longitude{0.0};                                              ///< Geo longitude for internal variables.
    double latitude{0.0};                                               ///< Geo latitude for internal variables.
    Qos subscribeQos{Qos::AtLeastOnce};                                ///< Requested QoS for subscriptions.
    bool logIncomingMessages{false};                                    ///< Logs all incoming messages handled by automation client.
    bool logOutgoingMessages{false};                                    ///< Logs all outgoing messages published by automation client.
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
    void handleDomainMessage(const Message& message);

    void refreshDynamicSubscriptionsLocked();
    void evaluateAndPublishRules();

    [[nodiscard]] bool isMonitoringTopic(const std::string& topicName) const;
    [[nodiscard]] bool isManagementTopic(const std::string& topicName) const;
    [[nodiscard]] bool isAutomationControlTopic(const std::string& topicName) const;

    [[nodiscard]] std::optional<std::string> extractRuleNameFromManagementTopic(
        const std::string& topicName) const;

    [[nodiscard]] static std::optional<std::string> extractMonitoringKeyPath(
        const std::string& payload);

    [[nodiscard]] static std::optional<std::string> extractMonitoringChangeType(
        const std::string& payload);

    [[nodiscard]] static std::optional<RuleTreeNode> parseJsonNode(const std::string& payload);
    [[nodiscard]] static std::string toJsonText(const RuleTreeNode& node);
    [[nodiscard]] static ExpressionEvaluator::Value messageValueToExpressionValue(const Value& messageValue);

    /**
     * @brief Converts message payload value into stable logging text.
     * @param messageValue MQTT message payload value.
     * @return Printable payload text.
     */
    [[nodiscard]] static std::string valueToLogText(const Value& messageValue);

    /**
     * @brief Converts QoS enum into numeric logging text.
     * @param qosValue QoS value from message.
     * @return QoS as decimal string.
     */
    [[nodiscard]] static std::string qosToLogText(Qos qosValue);

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

    void publishManagementAck(const std::string& ruleName, const std::string& payloadText) const;

    AutomationClientConfig config_{};

    mutable std::mutex stateMutex_;
    RuleTreeNode rulesRoot_{RuleTreeNode::Object{}};
    ExpressionEvaluator::VariableMap runtimeVariables_;
    std::set<std::string> dynamicTopicSubscriptions_;
    bool running_{false};

    mutable std::mutex publishMutex_;
    PublishCallback publishCallback_;
};

} // namespace yaha
