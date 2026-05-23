#include "yaha/automation_client/automation_rule_json.h"

#include <array>
#include <sstream>
#include <string>

#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation_client/automation_rule_tree_access.h"

namespace yaha::automation_rule_json {

namespace {

constexpr unsigned char k_control_char_upper_bound{0x20U};
constexpr unsigned char k_hex_low_nibble_mask{0x0FU};
constexpr unsigned char k_high_nibble_shift{4U};
const std::array<char, 16U> k_hex_digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

std::string escapeJsonString(const std::string& text) {
    std::string escaped{};
    escaped.reserve(text.size());
    for (const char currentChar : text) {
        switch (currentChar) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(currentChar) < k_control_char_upper_bound) {
                    escaped += "\\u00";
                    escaped.push_back(k_hex_digits[
                        (static_cast<unsigned char>(currentChar) >> k_high_nibble_shift) & k_hex_low_nibble_mask]);
                    escaped.push_back(k_hex_digits[static_cast<unsigned char>(currentChar) & k_hex_low_nibble_mask]);
                } else {
                    escaped.push_back(currentChar);
                }
                break;
        }
    }

    return escaped;
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
        return "\"" + escapeJsonString(std::get<std::string>(node.value)) + "\"";
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
