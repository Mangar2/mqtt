#include "yaha/automation_client/automation_rule_json.h"

#include "json/json_value.h"

#include <string>
#include <utility>

#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation_client/automation_rule_tree_access.h"

namespace yaha::automation_rule_json {

namespace {

[[nodiscard]] mqtt::json::JsonValue convertRuleTreeNodeToJsonValue(const RuleTreeNode& node) {
    if (std::holds_alternative<std::monostate>(node.value)) {
        return mqtt::json::JsonValue{};
    }
    if (std::holds_alternative<bool>(node.value)) {
        return mqtt::json::JsonValue{std::get<bool>(node.value)};
    }
    if (std::holds_alternative<double>(node.value)) {
        return mqtt::json::JsonValue{std::get<double>(node.value)};
    }
    if (std::holds_alternative<std::string>(node.value)) {
        return mqtt::json::JsonValue{std::get<std::string>(node.value)};
    }
    if (std::holds_alternative<RuleTreeNode::Array>(node.value)) {
        mqtt::json::JsonValue::Array arrayValue{};
        const auto& sourceArray = std::get<RuleTreeNode::Array>(node.value);
        arrayValue.reserve(sourceArray.size());
        for (const auto& entryNode : sourceArray) {
            arrayValue.push_back(convertRuleTreeNodeToJsonValue(entryNode));
        }
        return mqtt::json::JsonValue{std::move(arrayValue)};
    }

    mqtt::json::JsonValue::Object objectValue{};
    for (const auto& [keyText, valueNode] : std::get<RuleTreeNode::Object>(node.value)) {
        objectValue.emplace(keyText, convertRuleTreeNodeToJsonValue(valueNode));
    }
    return mqtt::json::JsonValue{std::move(objectValue)};
}

} // namespace

std::optional<RuleTreeNode> parseJsonNode(const std::string& payload) {
    const RuleTreeJsonReadResult readResult = RulesTreeJsonReader::parseJsonText(payload);
    if (!readResult.success || !readResult.errors.empty()) {
        return std::nullopt;
    }
    return readResult.root;
}

std::string toJsonText(const RuleTreeNode& node) {
    return convertRuleTreeNodeToJsonValue(node).stringify();
}

std::optional<std::string> extractStringFieldFromObjectPayload(
    const std::string& payload,
    const std::string& fieldName) {
    const std::optional<RuleTreeNode> parsed = parseJsonNode(payload);
    if (!parsed.has_value() || !parsed->isObject()) {
        return std::nullopt;
    }

    return automation_rule_tree_access::readStringField(parsed->asObject(), fieldName);
}

} // namespace yaha::automation_rule_json
