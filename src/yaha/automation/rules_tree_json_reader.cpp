#include "yaha/automation/rules_tree_json_reader.h"

#include "json/json_error.h"
#include "json/json_value.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace yaha {
namespace {

RuleTreeNode convertJsonValueToRuleTreeNode(const mqtt::json::JsonValue& jsonValue) {
    if (jsonValue.is_null()) {
        return RuleTreeNode{};
    }
    if (jsonValue.is_boolean()) {
        return RuleTreeNode{jsonValue.as_boolean()};
    }
    if (jsonValue.is_number()) {
        return RuleTreeNode{jsonValue.as_number()};
    }
    if (jsonValue.is_string()) {
        return RuleTreeNode{jsonValue.as_string()};
    }

    if (jsonValue.is_array()) {
        RuleTreeNode::Array arrayValue{};
        const auto& sourceArray = jsonValue.as_array();
        arrayValue.reserve(sourceArray.size());
        for (const auto& elementValue : sourceArray) {
            arrayValue.push_back(convertJsonValueToRuleTreeNode(elementValue));
        }
        return RuleTreeNode{std::move(arrayValue)};
    }
    RuleTreeNode::Object objectValue{};
    for (const auto& [keyText, entryValue] : jsonValue.as_object()) {
        objectValue.emplace(keyText, convertJsonValueToRuleTreeNode(entryValue));
    }
    return RuleTreeNode{std::move(objectValue)};
}

[[nodiscard]] std::pair<std::size_t, std::size_t> calculateLineAndColumn(
    const std::string_view text,
    const std::size_t offsetValue) {
    std::size_t lineValue{1U};
    std::size_t columnValue{1U};
    const std::size_t safeOffset = std::min(offsetValue, text.size());

    for (std::size_t index = 0U; index < safeOffset; ++index) {
        if (text[index] == '\n') {
            lineValue += 1U;
            columnValue = 1U;
            continue;
        }
        columnValue += 1U;
    }

    return {lineValue, columnValue};
}

[[nodiscard]] RuleTreeJsonReadError buildErrorFromJsonException(
    const mqtt::json::JsonException& exception,
    const std::string_view jsonText) {
    const auto [lineValue, columnValue] = calculateLineAndColumn(jsonText, exception.offset());
    return RuleTreeJsonReadError{
        .message = exception.what(),
        .line = lineValue,
        .column = columnValue};
}

[[nodiscard]] std::string readFileText(const std::string& filePath) {
    std::ifstream stream{filePath};
    if (!stream.is_open()) {
        throw std::runtime_error("failed to open json file: " + filePath);
    }

    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

} // namespace

RuleTreeJsonReadResult RulesTreeJsonReader::parseJsonText(const std::string& jsonText) {
    RuleTreeJsonReadResult result;

    try {
        const mqtt::json::JsonValue parsedRoot = mqtt::json::JsonValue::parse(jsonText);
        result.root = convertJsonValueToRuleTreeNode(parsedRoot);
        result.success = true;
        return result;
    } catch (const mqtt::json::JsonException& exception) {
        result.errors.push_back(buildErrorFromJsonException(exception, jsonText));
        return result;
    } catch (const std::exception& exception) {
        result.errors.push_back(RuleTreeJsonReadError{.message = exception.what(), .line = 0U, .column = 0U});
        return result;
    }
}

RuleTreeJsonReadResult RulesTreeJsonReader::parseJsonFile(const std::string& filePath) {
    RuleTreeJsonReadResult result;

    try {
        return parseJsonText(readFileText(filePath));
    } catch (const std::exception& exception) {
        result.errors.push_back(RuleTreeJsonReadError{.message = exception.what(), .line = 0U, .column = 0U});
        return result;
    }
}

} // namespace yaha
