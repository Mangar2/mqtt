#include "yaha/automation_client_rule_runtime/rule_field_access.h"

#include <variant>

namespace yaha {

std::optional<double> readNumberField(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end()) {
        return std::nullopt;
    }

    if (!std::holds_alternative<double>(fieldIter->second.value)) {
        return std::nullopt;
    }

    return std::get<double>(fieldIter->second.value);
}

std::vector<std::string> readTopicFilterList(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    std::vector<std::string> filters{};
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end()) {
        return filters;
    }

    if (fieldIter->second.isString()) {
        filters.push_back(fieldIter->second.asString());
        return filters;
    }

    if (!fieldIter->second.isArray()) {
        return filters;
    }

    for (const auto& arrayNode : fieldIter->second.asArray()) {
        if (!arrayNode.isString()) {
            return {};
        }
        filters.push_back(arrayNode.asString());
    }

    return filters;
}

std::optional<std::vector<std::string>> readTopicFilterArrayOnly(
    const RuleTreeNode::Object& ruleObject,
    const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    if (fieldIter == ruleObject.end() || !fieldIter->second.isArray()) {
        return std::nullopt;
    }

    std::vector<std::string> filters{};
    for (const auto& arrayNode : fieldIter->second.asArray()) {
        if (!arrayNode.isString()) {
            return std::nullopt;
        }
        filters.push_back(arrayNode.asString());
    }
    return filters;
}

bool fieldIsArray(const RuleTreeNode::Object& ruleObject, const std::string& fieldName) {
    const auto fieldIter = ruleObject.find(fieldName);
    return fieldIter != ruleObject.end() && fieldIter->second.isArray();
}

bool readActiveFlag(const RuleTreeNode::Object& ruleObject) {
    const auto activeIter = ruleObject.find("active");
    if (activeIter == ruleObject.end()) {
        return true;
    }

    if (!std::holds_alternative<bool>(activeIter->second.value)) {
        return true;
    }

    return std::get<bool>(activeIter->second.value);
}

} // namespace yaha
