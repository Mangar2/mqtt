#include "yaha/automation_client/automation_client_component.h"

#include "httplib.h"
#include "yaha/automation/internal_variables.h"
#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation/rules_tree_processor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr std::size_t k_management_suffix_length{4U};
constexpr int k_http_ok_status{200};
constexpr std::size_t k_max_pending_publish_attempts{3U};

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

[[nodiscard]] std::string publishFailureCategoryToText(const PublishFailureCategory categoryValue) {
    switch (categoryValue) {
    case PublishFailureCategory::None:
        return "none";
    case PublishFailureCategory::Disconnected:
        return "disconnected";
    case PublishFailureCategory::AckTimeout:
        return "ack_timeout";
    case PublishFailureCategory::WriteFailed:
        return "write_failed";
    case PublishFailureCategory::CallbackMissing:
        return "callback_missing";
    case PublishFailureCategory::Unknown:
        return "unknown";
    }

    return "unknown";
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

    for (const auto& topicFilter : config_.motionTopics) {
        subscriptions.insert({topicFilter, config_.subscribeQos});
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    for (const auto& topicFilter : dynamicTopicSubscriptions_) {
        subscriptions.insert({topicFilter, config_.subscribeQos});
    }

    return subscriptions;
}

void AutomationClientComponent::handleMessage(const Message& message) {
    processPendingPublishQueue();
    logIncomingMessageIfEnabled(message);

    if (isManagementTopic(message.topic())) {
        handleManagementMessage(message);
        return;
    }

    if (isMonitoringTopic(message.topic())) {
        handleMonitoringMessage(message);
        return;
    }

    handleDomainMessage(message);
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

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        runtimeVariables_[config_.presenceTopic] = std::string{"initial"};
    }

    processPendingPublishQueue();
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
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cerr << "automation_client[error] op=filestore_get path=" << config_.rulesKeyPath
                  << " status=" << statusText
                  << " reason=load_rules_failed"
                  << '\n'
                  << std::flush;
        return false;
    }

    const std::optional<RuleTreeNode> parsed = parseJsonNode(response->body);
    if (!parsed.has_value()) {
        std::cerr << "automation_client[error] op=filestore_get path=" << config_.rulesKeyPath
                  << " status=" << response->status
                  << " reason=invalid_json"
                  << '\n'
                  << std::flush;
        return false;
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    rulesRoot_ = parsed.value();
    RuleTreeNode::Object* rulesObject = ensureRulesObject(&rulesRoot_);
    (void)rulesObject;
    refreshDynamicSubscriptionsLocked();
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

    return persistRulesPayloadToFileStore(payloadText);
}

bool AutomationClientComponent::persistRulesPayloadToFileStore(const std::string& payloadText) const {
    if (!config_.fileStoreEnabled) {
        return true;
    }

    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    const auto response = client.Post(config_.rulesKeyPath, payloadText, "application/json");
    if (!response || response->status != k_http_ok_status) {
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cerr << "automation_client[error] op=filestore_post path=" << config_.rulesKeyPath
                  << " status=" << statusText
                  << " reason=persist_rules_failed"
                  << '\n'
                  << std::flush;
        return false;
    }

    return true;
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

    if (!loadRulesFromFileStore()) {
        std::cerr << "automation_client[error] op=monitor_reload keyPath=" << config_.rulesKeyPath
                  << " reason=reload_failed"
                  << '\n'
                  << std::flush;
    }
}

void AutomationClientComponent::handleManagementMessage(const Message& message) {
    const std::optional<std::string> ruleName = extractRuleNameFromManagementTopic(message.topic());
    if (!ruleName.has_value()) {
        return;
    }

    if (!std::holds_alternative<std::string>(message.value())) {
        publishManagementAck(*ruleName, "validation_failed");
        return;
    }

    const std::string& payloadText = std::get<std::string>(message.value());
    if (isDeletePayloadText(payloadText)) {
        RuleTreeNode stagedRulesRoot{};
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            stagedRulesRoot = rulesRoot_;
            RuleTreeNode::Object* rulesObject = ensureRulesObject(&stagedRulesRoot);
            rulesObject->erase(*ruleName);
        }

        if (!persistRulesPayloadToFileStore(toJsonText(stagedRulesRoot))) {
            publishManagementAck(*ruleName, "persist_failed");
            return;
        }

        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            rulesRoot_ = std::move(stagedRulesRoot);
            refreshDynamicSubscriptionsLocked();
        }

        publishManagementAck(*ruleName, "deleted");
        return;
    }

    const std::optional<RuleTreeNode> parsedRule = parseJsonNode(payloadText);
    if (!parsedRule.has_value() || !isRuleNodeStructureValid(*parsedRule)) {
        publishManagementAck(*ruleName, "validation_failed");
        return;
    }

    RuleTreeNode stagedRulesRoot{};
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        stagedRulesRoot = rulesRoot_;
    }

    {
        RuleTreeNode::Object* rulesObject = ensureRulesObject(&stagedRulesRoot);

        RuleTreeNode ruleNode = *parsedRule;
        auto& ruleObject = std::get<RuleTreeNode::Object>(ruleNode.value);
        if (!ruleObject.contains("name")) {
            ruleObject.insert({"name", RuleTreeNode{*ruleName}});
        }

        (*rulesObject)[*ruleName] = std::move(ruleNode);
    }

    if (!persistRulesPayloadToFileStore(toJsonText(stagedRulesRoot))) {
        publishManagementAck(*ruleName, "persist_failed");
        return;
    }

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        rulesRoot_ = std::move(stagedRulesRoot);
        refreshDynamicSubscriptionsLocked();
    }

    publishManagementAck(*ruleName, "updated");
}

void AutomationClientComponent::handleDomainMessage(const Message& message) {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        runtimeVariables_[message.topic()] = messageValueToExpressionValue(message.value());
    }

    evaluateAndPublishRules();
}

void AutomationClientComponent::refreshDynamicSubscriptionsLocked() {
    dynamicTopicSubscriptions_.clear();
    const RulesTreeParseResult parseResult = RulesTreeParser::parse(rulesRoot_);
    for (const auto& topicFilter : parseResult.externalVariables) {
        dynamicTopicSubscriptions_.insert(topicFilter);
    }
}

void AutomationClientComponent::evaluateAndPublishRules() {
    RuleTreeNode rulesSnapshot;
    ExpressionEvaluator::VariableMap variablesSnapshot;
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        rulesSnapshot = rulesRoot_;
        variablesSnapshot = runtimeVariables_;
    }

    try {
        const InternalVariables internalVariables{
            InternalVariables::GeoCoordinates{config_.longitude, config_.latitude}};
        const InternalVariables::VariableMap internalValues = internalVariables.calculate(
            std::chrono::system_clock::now());
        for (const auto& [variableName, variableValue] : internalValues) {
            if (std::holds_alternative<double>(variableValue)) {
                variablesSnapshot[variableName] = std::get<double>(variableValue);
            } else {
                variablesSnapshot[variableName] = std::get<std::chrono::system_clock::time_point>(
                    variableValue);
            }
        }
    } catch (...) {
        std::cerr << "automation_client[error] op=internal_variables reason=calculation_failed"
              << '\n'
              << std::flush;
    }

    const RulesTreeProcessingResult result = RulesTreeProcessor::process(rulesSnapshot, variablesSnapshot);
    if (result.messages.empty()) {
        return;
    }

    PublishCallback callback;
    {
        std::lock_guard<std::mutex> lock{publishMutex_};
        callback = publishCallback_;
    }

    if (!callback) {
        return;
    }

    for (const auto& outputMessage : result.messages) {
        try {
            callback(outputMessage);
            logOutgoingMessageIfEnabled(outputMessage);
        } catch (const std::exception& exceptionValue) {
            logOutgoingFailure(outputMessage, "publish_callback", exceptionValue.what());
            enqueuePendingPublish(outputMessage, "rule_output");
        } catch (...) {
            logOutgoingFailure(outputMessage, "publish_callback", "unknown");
            enqueuePendingPublish(outputMessage, "rule_output");
        }
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    for (const auto& outputMessage : result.messages) {
        runtimeVariables_[outputMessage.topic()] = messageValueToExpressionValue(outputMessage.value());
    }
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

ExpressionEvaluator::Value AutomationClientComponent::messageValueToExpressionValue(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }
    return std::get<double>(messageValue);
}

std::string AutomationClientComponent::valueToLogText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }

    std::ostringstream textStream;
    textStream << std::get<double>(messageValue);
    return textStream.str();
}

std::string AutomationClientComponent::qosToLogText(const Qos qosValue) {
    switch (qosValue) {
    case Qos::AtMostOnce:
        return "0";
    case Qos::AtLeastOnce:
        return "1";
    case Qos::ExactlyOnce:
        return "2";
    }

    return "unknown";
}

void AutomationClientComponent::logIncomingMessageIfEnabled(const Message& message) const {
    if (!config_.logIncomingMessages) {
        return;
    }

    std::cout << "automation_client[in] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << '\n'
              << std::flush;
}

void AutomationClientComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    if (!config_.logOutgoingMessages) {
        return;
    }

    std::cout << "automation_client[out] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << '\n'
              << std::flush;
}

void AutomationClientComponent::logOutgoingFailure(const Message& message,
                                                   const std::string& categoryText,
                                                   const std::string& reasonText) {
    std::cerr << "automation_client[out-fail] topic=" << message.topic()
              << " qos=" << qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToLogText(message.value())
              << " category=" << categoryText
              << " reason=" << reasonText
              << '\n'
              << std::flush;
}

bool AutomationClientComponent::tryPublishMessage(const Message& message,
                                                  const std::string& channelText) const {
    PublishCallback callback;
    {
        std::lock_guard<std::mutex> lock{publishMutex_};
        callback = publishCallback_;
    }

    if (!callback) {
        logOutgoingFailure(message, channelText, "callback_missing");
        return false;
    }

    try {
        const PublishResult publishResult = callback(message);
        if (!publishResult.success) {
            const std::string reasonText = publishResult.reason.empty()
                ? "unspecified"
                : publishResult.reason;
            logOutgoingFailure(message,
                               publishFailureCategoryToText(publishResult.category),
                               reasonText);
            return false;
        }

        logOutgoingMessageIfEnabled(message);
        return true;
    } catch (const std::exception& exceptionValue) {
        logOutgoingFailure(message, channelText, exceptionValue.what());
    } catch (...) {
        logOutgoingFailure(message, channelText, "unknown");
    }

    return false;
}

void AutomationClientComponent::enqueuePendingPublish(const Message& message,
                                                      const std::string& channelText) const {
    std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
    pendingPublishQueue_.push_back(PendingPublishEntry{message.clone(), channelText, 0U});
}

void AutomationClientComponent::processPendingPublishQueue() const {
    std::deque<PendingPublishEntry> pendingBatch{};
    {
        std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
        if (pendingPublishQueue_.empty()) {
            return;
        }
        pendingBatch.swap(pendingPublishQueue_);
    }

    for (auto& pendingEntry : pendingBatch) {
        if (tryPublishMessage(pendingEntry.message, pendingEntry.channelText)) {
            continue;
        }

        pendingEntry.attemptCount += 1U;
        if (pendingEntry.attemptCount >= k_max_pending_publish_attempts) {
            logOutgoingFailure(pendingEntry.message,
                               "retry_exhausted",
                               pendingEntry.channelText);
            continue;
        }

        std::lock_guard<std::mutex> lock{pendingPublishQueueMutex_};
        pendingPublishQueue_.push_back(std::move(pendingEntry));
    }
}

void AutomationClientComponent::publishManagementAck(
    const std::string& ruleName,
    const std::string& payloadText) const {
    const Message ackMessage{
        config_.managementTopicPrefix + "/" + ruleName,
        payloadText,
        Qos::AtLeastOnce,
        false};

    if (!tryPublishMessage(ackMessage, "management_ack")) {
        enqueuePendingPublish(ackMessage, "management_ack");
    }
}

} // namespace yaha
