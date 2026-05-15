#include "yaha/automation_client/automation_rule_json.h"

#include <sstream>
#include <string>

#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation_client/automation_rule_tree_access.h"

namespace yaha::automation_rule_json {

std::optional<RuleTreeNode> parseJsonNode(const std::string& payload) {
    const RuleTreeJsonReadResult readResult = RulesTreeJsonReader::parseJsonText(payload);
    if (!readResult.success || !readResult.errors.empty()) {
        return std::nullopt;
    }
    return readResult.root;
}

std::string toJsonText(const RuleTreeNode& node) {
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
