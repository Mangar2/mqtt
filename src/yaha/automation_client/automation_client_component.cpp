#include "yaha/automation_client/automation_client_component.h"

#include "httplib.h"
#include "yaha/automation/rules_tree_json_reader.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr std::size_t k_management_suffix_length{4U};
constexpr int k_http_ok_status{200};

[[nodiscard]] bool startsWithText(const std::string& textValue, const std::string& prefix) {
    return textValue.size() >= prefix.size() && textValue.compare(0U, prefix.size(), prefix) == 0;
}

[[nodiscard]] bool endsWithSetSuffix(const std::string& textValue) {
    return textValue.size() >= k_management_suffix_length
        && textValue.compare(textValue.size() - k_management_suffix_length, k_management_suffix_length, "/set") == 0;
}

[[nodiscard]] bool isDeletePayloadText(const std::string& payload) {
    std::string trimmed = payload;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char currentChar) {
        return std::isspace(currentChar) == 0;
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char currentChar) {
        return std::isspace(currentChar) == 0;
    }).base(), trimmed.end());
    return trimmed == "delete";
}

[[nodiscard]] bool isRuleNodeStructureValid(const RuleTreeNode& node) {
    return node.isObject()
        && node.asObject().contains("topic")
        && node.asObject().at("topic").isString();
}

[[nodiscard]] RuleTreeNode::Object* ensureRulesObject(RuleTreeNode* rootNode) {
    if (!rootNode->isObject()) {
        rootNode->value = RuleTreeNode::Object{};
    }

    auto& rootObject = std::get<RuleTreeNode::Object>(rootNode->value);
    if (!rootObject.contains("rules") || !rootObject["rules"].isObject()) {
        rootObject["rules"] = RuleTreeNode{RuleTreeNode::Object{}};
    }

    return &std::get<RuleTreeNode::Object>(rootObject["rules"].value);
}

[[nodiscard]] std::optional<std::string> readStringField(
    const RuleTreeNode::Object& objectNode,
    const std::string& fieldName) {
    if (!objectNode.contains(fieldName)) {
        return std::nullopt;
    }
    const RuleTreeNode& fieldNode = objectNode.at(fieldName);
    if (!fieldNode.isString()) {
        return std::nullopt;
    }
    return fieldNode.asString();
}

} // namespace

AutomationClientComponent::AutomationClientComponent(AutomationClientConfig config)
    : config_(std::move(config)) {
}

SubscriptionMap AutomationClientComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({config_.managementTopicPrefix + "/#", config_.subscribeQos});
    subscriptions.insert({config_.monitorTopicPrefix + "/#", config_.subscribeQos});
    return subscriptions;
}

void AutomationClientComponent::handleMessage(const Message& message) {
    if (isManagementTopic(message.topic())) {
        handleManagementMessage(message);
        return;
    }

    if (isMonitoringTopic(message.topic())) {
        handleMonitoringMessage(message);
    }
}

void AutomationClientComponent::run() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
    }

    if (config_.fileStoreEnabled) {
        (void)loadRulesFromFileStore();
    }
}

void AutomationClientComponent::close() {
    std::lock_guard<std::mutex> lock{stateMutex_};
    running_ = false;
}

void AutomationClientComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{publishMutex_};
    publishCallback_ = std::move(callback);
}

bool AutomationClientComponent::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

std::size_t AutomationClientComponent::ruleCount() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    if (!rulesRoot_.isObject()) {
        return 0U;
    }

    const auto& rootObject = rulesRoot_.asObject();
    if (!rootObject.contains("rules") || !rootObject.at("rules").isObject()) {
        return 0U;
    }

    return rootObject.at("rules").asObject().size();
}

bool AutomationClientComponent::hasRule(const std::string& ruleName) const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    if (!rulesRoot_.isObject()) {
        return false;
    }

    const auto& rootObject = rulesRoot_.asObject();
    if (!rootObject.contains("rules") || !rootObject.at("rules").isObject()) {
        return false;
    }

    return rootObject.at("rules").asObject().contains(ruleName);
}

bool AutomationClientComponent::loadRulesFromFileStore() {
    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    const auto response = client.Get(config_.rulesKeyPath);
    if (!response || response->status != k_http_ok_status) {
        return false;
    }

    const std::optional<RuleTreeNode> parsed = parseJsonNode(response->body);
    if (!parsed.has_value()) {
        return false;
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    rulesRoot_ = parsed.value();
    RuleTreeNode::Object* rulesObject = ensureRulesObject(&rulesRoot_);
    (void)rulesObject;
    return true;
}

bool AutomationClientComponent::persistRulesToFileStore() const {
    if (!config_.fileStoreEnabled) {
        return true;
    }

    const std::string payloadText = [this]() {
        std::lock_guard<std::mutex> lock{stateMutex_};
        return toJsonText(rulesRoot_);
    }();

    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    const auto response = client.Post(config_.rulesKeyPath, payloadText, "application/json");
    return response && response->status == k_http_ok_status;
}

void AutomationClientComponent::handleMonitoringMessage(const Message& message) {
    if (!std::holds_alternative<std::string>(message.value())) {
        return;
    }

    const std::string& payloadText = std::get<std::string>(message.value());
    const std::optional<std::string> keyPath = extractMonitoringKeyPath(payloadText);
    if (!keyPath.has_value() || *keyPath != config_.rulesKeyPath) {
        return;
    }

    const std::optional<std::string> changeType = extractMonitoringChangeType(payloadText);
    if (changeType.has_value() && *changeType == "deleted") {
        return;
    }

    (void)loadRulesFromFileStore();
}

void AutomationClientComponent::handleManagementMessage(const Message& message) {
    const std::optional<std::string> ruleName = extractRuleNameFromManagementTopic(message.topic());
    if (!ruleName.has_value()) {
        return;
    }

    if (!std::holds_alternative<std::string>(message.value())) {
        publishManagementAck(*ruleName, "invalid rule");
        return;
    }

    const std::string& payloadText = std::get<std::string>(message.value());
    std::string acknowledgedRuleJson{};
    if (isDeletePayloadText(payloadText)) {
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            RuleTreeNode::Object* rulesObject = ensureRulesObject(&rulesRoot_);
            rulesObject->erase(*ruleName);
        }

        if (persistRulesToFileStore()) {
            publishManagementAck(*ruleName, "deleted");
        }
        return;
    }

    const std::optional<RuleTreeNode> parsedRule = parseJsonNode(payloadText);
    if (!parsedRule.has_value() || !isRuleNodeStructureValid(*parsedRule)) {
        publishManagementAck(*ruleName, "invalid rule");
        return;
    }

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        RuleTreeNode::Object* rulesObject = ensureRulesObject(&rulesRoot_);

        RuleTreeNode ruleNode = *parsedRule;
        auto& ruleObject = std::get<RuleTreeNode::Object>(ruleNode.value);
        if (!ruleObject.contains("name")) {
            ruleObject.insert({"name", RuleTreeNode{*ruleName}});
        }

        (*rulesObject)[*ruleName] = std::move(ruleNode);
        acknowledgedRuleJson = toJsonText(rulesObject->at(*ruleName));
    }

    if (!persistRulesToFileStore()) {
        publishManagementAck(*ruleName, "invalid rule");
        return;
    }

    publishManagementAck(*ruleName, acknowledgedRuleJson);
}

bool AutomationClientComponent::isMonitoringTopic(const std::string& topicName) const {
    return startsWithText(topicName, config_.monitorTopicPrefix + "/");
}

bool AutomationClientComponent::isManagementTopic(const std::string& topicName) const {
    return startsWithText(topicName, config_.managementTopicPrefix + "/") && endsWithSetSuffix(topicName);
}

std::optional<std::string> AutomationClientComponent::extractRuleNameFromManagementTopic(
    const std::string& topicName) const {
    const std::string prefix = config_.managementTopicPrefix + "/";
    if (!startsWithText(topicName, prefix) || !endsWithSetSuffix(topicName)) {
        return std::nullopt;
    }

    const std::size_t startIndex = prefix.size();
    const std::size_t endIndex = topicName.size() - k_management_suffix_length;
    if (endIndex <= startIndex) {
        return std::nullopt;
    }

    return topicName.substr(startIndex, endIndex - startIndex);
}

std::optional<std::string> AutomationClientComponent::extractMonitoringKeyPath(const std::string& payload) {
    const std::optional<RuleTreeNode> parsed = parseJsonNode(payload);
    if (!parsed.has_value() || !parsed->isObject()) {
        return std::nullopt;
    }

    return readStringField(parsed->asObject(), "keyPath");
}

std::optional<std::string> AutomationClientComponent::extractMonitoringChangeType(const std::string& payload) {
    const std::optional<RuleTreeNode> parsed = parseJsonNode(payload);
    if (!parsed.has_value() || !parsed->isObject()) {
        return std::nullopt;
    }

    return readStringField(parsed->asObject(), "changeType");
}

std::optional<RuleTreeNode> AutomationClientComponent::parseJsonNode(const std::string& payload) {
    const RuleTreeJsonReadResult readResult = RulesTreeJsonReader::parseJsonText(payload);
    if (!readResult.success || !readResult.errors.empty()) {
        return std::nullopt;
    }
    return readResult.root;
}

std::string AutomationClientComponent::toJsonText(const RuleTreeNode& node) {
    if (std::holds_alternative<std::monostate>(node.value)) {
        return "null";
    }
    if (std::holds_alternative<bool>(node.value)) {
        return std::get<bool>(node.value) ? "true" : "false";
    }
    if (std::holds_alternative<double>(node.value)) {
        std::ostringstream stream;
        stream << std::get<double>(node.value);
        return stream.str();
    }
    if (std::holds_alternative<std::string>(node.value)) {
        std::string escaped{"\""};
        for (const char currentChar : std::get<std::string>(node.value)) {
            if (currentChar == '\\' || currentChar == '\"') {
                escaped.push_back('\\');
            }
            escaped.push_back(currentChar);
        }
        escaped.push_back('\"');
        return escaped;
    }
    if (std::holds_alternative<RuleTreeNode::Array>(node.value)) {
        const auto& arrayValue = std::get<RuleTreeNode::Array>(node.value);
        std::string jsonText{"["};
        for (std::size_t index = 0U; index < arrayValue.size(); ++index) {
            if (index > 0U) {
                jsonText.push_back(',');
            }
            jsonText.append(toJsonText(arrayValue[index]));
        }
        jsonText.push_back(']');
        return jsonText;
    }

    const auto& objectValue = std::get<RuleTreeNode::Object>(node.value);
    std::string jsonText{"{"};
    bool firstEntry = true;
    for (const auto& [keyText, valueNode] : objectValue) {
        if (!firstEntry) {
            jsonText.push_back(',');
        }
        firstEntry = false;
        jsonText.append(toJsonText(RuleTreeNode{std::string{keyText}}));
        jsonText.push_back(':');
        jsonText.append(toJsonText(valueNode));
    }
    jsonText.push_back('}');
    return jsonText;
}

void AutomationClientComponent::publishManagementAck(
    const std::string& ruleName,
    const std::string& payloadText) const {
    PublishCallback callback;
    {
        std::lock_guard<std::mutex> lock{publishMutex_};
        callback = publishCallback_;
    }

    if (!callback) {
        return;
    }

    try {
        callback(Message{
            config_.managementTopicPrefix + "/" + ruleName,
            payloadText,
            Qos::AtLeastOnce,
            false});
    } catch (...) {
    }
}

} // namespace yaha
