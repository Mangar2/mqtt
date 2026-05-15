#include "yaha/automation_client/automation_rule_tree_access.h"

namespace yaha::automation_rule_tree_access {

RuleTreeNode::Object* ensureRulesObject(RuleTreeNode* rootNode) {
    if (!rootNode->isObject()) {
        rootNode->value = RuleTreeNode::Object{};
    }

    auto& rootObject = std::get<RuleTreeNode::Object>(rootNode->value);
    if (!rootObject.contains("rules") || !rootObject["rules"].isObject()) {
        rootObject["rules"] = RuleTreeNode{RuleTreeNode::Object{}};
    }

    return &std::get<RuleTreeNode::Object>(rootObject["rules"].value);
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
