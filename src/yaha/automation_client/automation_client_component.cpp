#include "yaha/automation_client/automation_client_component.h"

#include "httplib.h"
#include "yaha/automation/internal_variables.h"
#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation/single_rule_processor.h"
#include "yaha/automation_client/automation_control_topics.h"
#include "yaha/automation_client/automation_message_values.h"
#include "yaha/automation_client/automation_publish_failure_text.h"
#include "yaha/automation_client/automation_rule_json.h"
#include "yaha/automation_client/automation_rule_lookup.h"
#include "yaha/automation_client/automation_rule_tree_access.h"
#include "yaha/automation_client/automation_trace_format.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <ranges>
#include <string>
#include <utility>

namespace yaha {
namespace {

constexpr int k_http_ok_status{200};
constexpr int k_http_not_found_status{404};
constexpr std::size_t k_max_pending_publish_attempts{3U};
constexpr std::string_view k_debug_topic_prefix{"$MONITOR/automation/"};

struct RuleValidationResult {
    bool isValid{false};
    std::vector<std::string> errors;
};

[[nodiscard]] RuleValidationResult validateIncomingRule(const RuleTreeNode& ruleNode) {
    RuleValidationResult result{};
    if (!ruleNode.isObject()) {
        result.errors.emplace_back("rule payload must be a JSON object");
        return result;
    }

    if (!RuleRuntimeEngine::isRuleNodeStructureValid(ruleNode)) {
        result.errors.emplace_back("invalid rule: required field 'topic' is missing or malformed");
        return result;
    }

    result.isValid = true;
    return result;
}
} // namespace

AutomationClientComponent::AutomationClientComponent(AutomationClientConfig config)
    : config_(std::move(config)) {
}

SubscriptionMap AutomationClientComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({config_.managementTopicPrefix + "/#", config_.subscribeQos});
    subscriptions.insert({config_.monitorTopicPrefix + "/#", config_.subscribeQos});
    subscriptions.insert({"$MONITOR/automation/#", config_.subscribeQos});

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

    if (automation_control_topics::isDebugTopic(message.topic(), std::string{k_debug_topic_prefix})) {
        handleDebugMessage(message);
        return;
    }

    if (automation_control_topics::isManagementTopic(message.topic(), config_.managementTopicPrefix)) {
        handleManagementMessage(message);
        return;
    }

    if (automation_control_topics::isMonitoringTopic(message.topic(), config_.monitorTopicPrefix)) {
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
        runtimeVariables_["status/presence"] = std::string{"initial"};
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
    if (response && response->status == k_http_not_found_status) {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (!rulesRoot_.isObject()) {
            rulesRoot_ = RuleTreeNode{RuleTreeNode::Object{}};
        }
        RuleTreeNode::Object* rulesObject = automation_rule_tree_access::ensureRulesObject(&rulesRoot_);
        (void)rulesObject;
        refreshDynamicSubscriptionsLocked();
        return true;
    }

    if (!response || response->status != k_http_ok_status) {
        const std::string statusText = response ? std::to_string(response->status) : "no_response";
        std::cerr << "automation_client[error] op=filestore_get path=" << config_.rulesKeyPath
                  << " status=" << statusText
                  << " reason=load_rules_failed"
                  << '\n'
                  << std::flush;
        return false;
    }

    const std::optional<RuleTreeNode> parsed = automation_rule_json::parseJsonNode(response->body);
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
    RuleTreeNode::Object* rulesObject = automation_rule_tree_access::ensureRulesObject(&rulesRoot_);
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
        return automation_rule_json::toJsonText(rulesRoot_);
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

    const auto& payloadText = std::get<std::string>(message.value());
    const std::optional<std::string> keyPath = automation_rule_json::extractStringFieldFromObjectPayload(
        payloadText,
        "keyPath");
    if (!keyPath.has_value() || *keyPath != config_.rulesKeyPath) {
        return;
    }

    const std::optional<std::string> changeType = automation_rule_json::extractStringFieldFromObjectPayload(
        payloadText,
        "changeType");
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
    const std::optional<std::string> ruleName = automation_control_topics::extractRuleNameFromManagementTopic(
        message.topic(),
        config_.managementTopicPrefix);
    if (!ruleName.has_value()) {
        return;
    }

    if (!std::holds_alternative<std::string>(message.value())) {
        publishManagementAck(*ruleName, "validation_failed");
        return;
    }

    const auto& payloadText = std::get<std::string>(message.value());
    if (automation_control_topics::isDeletePayloadText(payloadText)) {
        RuleTreeNode stagedRulesRoot{};
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            stagedRulesRoot = rulesRoot_;
            RuleTreeNode::Object* rulesObject = automation_rule_tree_access::ensureRulesObject(&stagedRulesRoot);
            rulesObject->erase(*ruleName);
        }

        if (!persistRulesPayloadToFileStore(automation_rule_json::toJsonText(stagedRulesRoot))) {
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

    const RuleTreeJsonReadResult readResult = RulesTreeJsonReader::parseJsonText(payloadText);
    if (!readResult.success || !readResult.errors.empty()) {
        publishManagementAck(*ruleName, "validation_failed");
        return;
    }

    RuleTreeNode ruleNode = readResult.root;
    RuleValidationResult validation = validateIncomingRule(ruleNode);
    auto& ruleObject = std::get<RuleTreeNode::Object>(ruleNode.value);
    if (!ruleObject.contains("name")) {
        ruleObject.insert({"name", RuleTreeNode{*ruleName}});
    }
    ruleObject["isValid"] = RuleTreeNode{validation.isValid};
    if (!validation.isValid) {
        RuleTreeNode::Array errorNodes{};
        errorNodes.reserve(validation.errors.size());
        for (const auto& errorText : validation.errors) {
            errorNodes.emplace_back(errorText);
        }
        ruleObject["errors"] = RuleTreeNode{std::move(errorNodes)};
    }

    RuleTreeNode stagedRulesRoot{};
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        stagedRulesRoot = rulesRoot_;
    }

    {
        RuleTreeNode::Object* rulesObject = automation_rule_tree_access::ensureRulesObject(&stagedRulesRoot);
        (*rulesObject)[*ruleName] = std::move(ruleNode);
    }

    RuleTreeNode::Object* stagedRulesObject = automation_rule_tree_access::ensureRulesObject(&stagedRulesRoot);
    if (!stagedRulesObject->contains(*ruleName)) {
        publishManagementAck(*ruleName, "persist_failed");
        return;
    }
    const std::string rulePayload = automation_rule_json::toJsonText(stagedRulesObject->at(*ruleName));

    if (!persistRulesPayloadToFileStore(automation_rule_json::toJsonText(stagedRulesRoot))) {
        publishManagementAck(*ruleName, "persist_failed");
        return;
    }

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        rulesRoot_ = std::move(stagedRulesRoot);
        refreshDynamicSubscriptionsLocked();
    }

    publishManagementAck(*ruleName, rulePayload);
    if (!validation.isValid) {
        publishManagementAck(*ruleName, "invalid rule");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void AutomationClientComponent::handleDebugMessage(const Message& message) {
    std::vector<std::string> traceEntries{};

    const std::optional<std::string> ruleLink = automation_control_topics::extractRuleLinkFromDebugTopic(
        message.topic(),
        std::string{k_debug_topic_prefix});

    std::string resolvedRulePath{};
    std::optional<RuleTreeNode> ruleNode;
    ExpressionEvaluator::VariableMap variablesSnapshot;
    RuleRuntimeDeliveryState deliveryStateSnapshot;

    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (ruleLink.has_value()) {
            ruleNode = findRuleNodeByLink(*ruleLink, &resolvedRulePath);
        }
        variablesSnapshot = runtimeVariables_;
        deliveryStateSnapshot = runtimeDeliveryState_;
    }

    std::string traceValue{"error"};

    if (!ruleLink.has_value()) {
        automation_trace_format::appendTraceEntry(&traceEntries, "debug:error invalid debug topic shape");
    } else if (!ruleNode.has_value()) {
        automation_trace_format::appendTraceEntry(&traceEntries, "debug:rule lookup failed link=" + *ruleLink);
    } else {
        automation_trace_format::appendTraceEntry(&traceEntries, "debug:rule path=" + resolvedRulePath);
        const auto evaluationTime = std::chrono::system_clock::now();
        try {
            const InternalVariables internalVariables{
                InternalVariables::GeoCoordinates{.longitude = config_.longitude, .latitude = config_.latitude}};
            const InternalVariables::VariableMap internalValues = internalVariables.calculate(
                evaluationTime);
            for (const auto& [variableName, variableValue] : internalValues) {
                if (std::holds_alternative<double>(variableValue)) {
                    variablesSnapshot[variableName] = std::get<double>(variableValue);
                } else {
                    variablesSnapshot[variableName] = std::get<std::chrono::system_clock::time_point>(
                        variableValue);
                }
            }
        } catch (...) {
            automation_trace_format::appendTraceEntry(&traceEntries, "debug:error internal variable calculation failed");
        }

        std::vector<std::string> evaluationTrace{};
        const SingleRuleProcessingResult result = SingleRuleProcessor::processWithTrace(
            *ruleNode,
            variablesSnapshot,
            &evaluationTrace,
            resolvedRulePath);
        automation_trace_format::appendExplainTraceEntries(&traceEntries, evaluationTrace, resolvedRulePath);

        if (!result.success) {
            traceValue = "error";
            if (!result.errors.empty()) {
                automation_trace_format::appendTraceEntry(&traceEntries, "debug:error " + result.errors.front());
            }
        } else if (!result.triggered || (result.messages.empty() && !result.message.has_value())) {
            traceValue = "not_triggered";
            automation_trace_format::appendTraceEntry(&traceEntries, "debug:result no outbound message");
        } else {
            traceValue = "triggered";
            const std::vector<Message> deliveryCandidates = !result.messages.empty()
                ? result.messages
                : std::vector<Message>{result.message.value().clone()};
            const std::vector<Message> deliveredMessages = RuleRuntimeEngine::previewDeliveredMessages(
                resolvedRulePath,
                *ruleNode,
                deliveryCandidates,
                evaluationTime,
                deliveryStateSnapshot);

            const std::size_t candidateCount = deliveryCandidates.size();
            const std::size_t deliveredCount = deliveredMessages.size();
            if (candidateCount == 1U) {
                automation_trace_format::appendTraceEntry(
                    &traceEntries,
                    "debug:result outbound topic=" + deliveryCandidates.front().topic()
                        + ", would send=" + std::to_string(deliveredCount)
                        + (deliveredCount > 0U
                               ? " (delivery controls allow)"
                               : " (delivery controls suppress: dedup/delay/cooldown)"));
            } else {
                automation_trace_format::appendTraceEntry(
                    &traceEntries,
                    "debug:result outbound topics=" + std::to_string(candidateCount)
                        + ", would send=" + std::to_string(deliveredCount)
                        + (deliveredCount > 0U
                               ? " (delivery controls allow)"
                               : " (delivery controls suppress: dedup/delay/cooldown)"));
            }
        }
    }

    const std::string traceTopic = automation_control_topics::buildTraceTopicFromRuleLink(
        std::string{k_debug_topic_prefix},
        ruleLink.value_or(std::string{"invalid"}));
    Message traceMessage{traceTopic, traceValue, Qos::AtLeastOnce, false};
    for (const auto& traceEntry : traceEntries | std::views::reverse) {
        traceMessage.addReason(traceEntry);
    }
    traceMessage.setRawPayload(automation_trace_format::buildTraceRawPayload(traceMessage));

    if (!tryPublishMessage(traceMessage, "debug_trace")) {
        enqueuePendingPublish(traceMessage, "debug_trace");
    }
}

void AutomationClientComponent::handleDomainMessage(const Message& message) {
    const auto nowTime = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        runtimeVariables_[message.topic()] = automation_message_values::toExpressionValue(message.value());
        RuleRuntimeEngine::ingestDomainMessageEvent(
            message,
            nowTime,
            config_.motionTopics,
            &runtimeEventState_);
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
    ExpressionEvaluator::VariableMap variablesSnapshot;
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        variablesSnapshot = runtimeVariables_;
    }

    const auto evaluationTime = std::chrono::system_clock::now();

    try {
        const InternalVariables internalVariables{
            InternalVariables::GeoCoordinates{.longitude = config_.longitude, .latitude = config_.latitude}};
        const InternalVariables::VariableMap internalValues = internalVariables.calculate(evaluationTime);
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

    RuleRuntimeProcessingResult result;
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        result = RuleRuntimeEngine::processRules(
            rulesRoot_,
            variablesSnapshot,
            evaluationTime,
            &runtimeEventState_,
            &runtimeDeliveryState_);
        RuleRuntimeEngine::clearNonMotionEvents(&runtimeEventState_);
    }

    if (result.messages.empty()) {
        return;
    }

    for (const auto& outputMessage : result.messages) {
        if (!tryPublishMessage(outputMessage, "rule_output")) {
            enqueuePendingPublish(outputMessage, "rule_output");
        }
    }

    std::lock_guard<std::mutex> lock{stateMutex_};
    for (const auto& outputMessage : result.messages) {
        runtimeVariables_[outputMessage.topic()] = automation_message_values::toExpressionValue(outputMessage.value());
    }
}

std::optional<RuleTreeNode> AutomationClientComponent::findRuleNodeByLink(
    const std::string& ruleLink,
    std::string* resolvedPath) const {
    const std::vector<std::string> ruleSegments = automation_rule_lookup::splitPathSegments(ruleLink);
    if (ruleSegments.empty()) {
        return std::nullopt;
    }

    auto setResolvedPath = [resolvedPath](const std::string& pathText) {
        if (resolvedPath != nullptr) {
            *resolvedPath = pathText;
        }
    };

    const std::vector<std::vector<std::string>> candidatePaths = automation_rule_lookup::buildRuleLookupCandidates(ruleSegments);
    for (const auto& candidatePath : candidatePaths) {
        const std::optional<RuleTreeNode> candidateNode = automation_rule_lookup::findNodeByPathSegments(rulesRoot_, candidatePath);
        if (!candidateNode.has_value()) {
            continue;
        }
        setResolvedPath(automation_rule_lookup::joinPathSegments(candidatePath));
        return candidateNode;
    }

    return std::nullopt;
}


void AutomationClientComponent::logIncomingMessageIfEnabled(const Message& message) const {
    if (!config_.logIncomingMessages) {
        return;
    }

    std::cout << "automation_client[in] topic=" << message.topic()
              << " qos=" << automation_message_values::qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << automation_message_values::valueToLogText(message.value())
              << '\n';
    for (const auto& entry : message.reason()) {
        std::cout << "  reason: [" << entry.timestamp << "] " << entry.message << '\n';
    }
    std::cout << std::flush;
}

void AutomationClientComponent::logOutgoingMessageIfEnabled(const Message& message) const {
    if (!config_.logOutgoingMessages) {
        return;
    }

    std::cout << "automation_client[out] topic=" << message.topic()
              << " qos=" << automation_message_values::qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << automation_message_values::valueToLogText(message.value())
              << '\n';
    for (const auto& entry : message.reason()) {
        std::cout << "  reason: [" << entry.timestamp << "] " << entry.message << '\n';
    }
    std::cout << std::flush;
}

void AutomationClientComponent::logOutgoingFailure(const Message& message,
                                                   const std::string& categoryText,
                                                   const std::string& reasonText) {
    std::cerr << "automation_client[out-fail] topic=" << message.topic()
              << " qos=" << automation_message_values::qosToLogText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << automation_message_values::valueToLogText(message.value())
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
            logOutgoingFailure(
                message,
                automation_publish_failure_text::toText(publishResult.category),
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
    pendingPublishQueue_.push_back(PendingPublishEntry{
        .message = message.clone(),
        .channelText = channelText,
        .attemptCount = 0U});
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
