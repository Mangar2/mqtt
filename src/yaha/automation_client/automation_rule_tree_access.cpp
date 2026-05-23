#include "yaha/automation_client/automation_rule_tree_access.h"

namespace yaha::automation_rule_tree_access {

namespace {

RuleTreeNode::Object* ensureObjectNode(RuleTreeNode* node) {
    if (!node->isObject()) {
        node->value = RuleTreeNode::Object{};
    }
    return &std::get<RuleTreeNode::Object>(node->value);
}

} // namespace

RuleTreeNode::Object* ensureRulesObject(RuleTreeNode* rootNode) {
    auto& rootObject = *ensureObjectNode(rootNode);
    if (!rootObject.contains("rules") || !rootObject["rules"].isObject()) {
        rootObject["rules"] = RuleTreeNode{RuleTreeNode::Object{}};
    }

    return &std::get<RuleTreeNode::Object>(rootObject["rules"].value);
}

RuleTreeNode::Object* ensureRuleContainerByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& parentPathSegments) {
    RuleTreeNode* currentNode = rootNode;
    for (const auto& pathSegment : parentPathSegments) {
        auto& currentObject = *ensureObjectNode(currentNode);
        if (!currentObject.contains(pathSegment) || !currentObject[pathSegment].isObject()) {
            currentObject[pathSegment] = RuleTreeNode{RuleTreeNode::Object{}};
        }
        currentNode = &currentObject[pathSegment];
    }

    return ensureRulesObject(currentNode);
}

RuleTreeNode::Object* findRuleContainerByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& parentPathSegments) {
    RuleTreeNode* currentNode = rootNode;
    for (const auto& pathSegment : parentPathSegments) {
        if (!currentNode->isObject()) {
            return nullptr;
        }

        auto& currentObject = std::get<RuleTreeNode::Object>(currentNode->value);
        const auto iterator = currentObject.find(pathSegment);
        if (iterator == currentObject.end() || !iterator->second.isObject()) {
            return nullptr;
        }

        currentNode = &iterator->second;
    }

    if (!currentNode->isObject()) {
        return nullptr;
    }

    auto& parentObject = std::get<RuleTreeNode::Object>(currentNode->value);
    const auto rulesIterator = parentObject.find("rules");
    if (rulesIterator == parentObject.end() || !rulesIterator->second.isObject()) {
        return nullptr;
    }

    return &std::get<RuleTreeNode::Object>(rulesIterator->second.value);
}

const RuleTreeNode* findRuleByPath(
    const RuleTreeNode& rootNode,
    const std::vector<std::string>& fullRulePathSegments) {
    if (fullRulePathSegments.empty()) {
        return nullptr;
    }

    const std::vector<std::string> parentPathSegments{
        fullRulePathSegments.begin(),
        std::prev(fullRulePathSegments.end())};
    const std::string& ruleName = fullRulePathSegments.back();

    const RuleTreeNode* currentNode = &rootNode;
    for (const auto& pathSegment : parentPathSegments) {
        if (!currentNode->isObject()) {
            return nullptr;
        }

        const auto& currentObject = std::get<RuleTreeNode::Object>(currentNode->value);
        const auto iterator = currentObject.find(pathSegment);
        if (iterator == currentObject.end() || !iterator->second.isObject()) {
            return nullptr;
        }

        currentNode = &iterator->second;
    }

    if (!currentNode->isObject()) {
        return nullptr;
    }

    const auto& parentObject = std::get<RuleTreeNode::Object>(currentNode->value);
    const auto rulesIterator = parentObject.find("rules");
    if (rulesIterator == parentObject.end() || !rulesIterator->second.isObject()) {
        return nullptr;
    }

    const auto& rulesObject = std::get<RuleTreeNode::Object>(rulesIterator->second.value);
    const auto ruleIterator = rulesObject.find(ruleName);
    if (ruleIterator == rulesObject.end()) {
        return nullptr;
    }

    return &ruleIterator->second;
}

void upsertRuleByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& fullRulePathSegments,
    RuleTreeNode ruleNode) {
    if (fullRulePathSegments.empty()) {
        return;
    }

    const std::vector<std::string> parentPathSegments{
        fullRulePathSegments.begin(),
        std::prev(fullRulePathSegments.end())};
    const std::string& ruleName = fullRulePathSegments.back();
    RuleTreeNode::Object* rulesObject = ensureRuleContainerByPath(rootNode, parentPathSegments);
    (*rulesObject)[ruleName] = std::move(ruleNode);
}

bool eraseRuleByPath(
    RuleTreeNode* rootNode,
    const std::vector<std::string>& fullRulePathSegments) {
    if (fullRulePathSegments.empty()) {
        return false;
    }

    const std::vector<std::string> parentPathSegments{
        fullRulePathSegments.begin(),
        std::prev(fullRulePathSegments.end())};
    const std::string& ruleName = fullRulePathSegments.back();
    RuleTreeNode::Object* rulesObject = findRuleContainerByPath(rootNode, parentPathSegments);
    if (rulesObject == nullptr) {
        return false;
    }

    return rulesObject->erase(ruleName) > 0U;
}

std::optional<std::string> readStringField(
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

} // namespace yaha::automation_rule_tree_access
