#include "yaha/automation/rules_tree_parser.h"

#include <utility>

#include "yaha/automation/expression_parser.h"

namespace yaha {

RuleTreeNode::RuleTreeNode(const bool booleanValue)
    : value(booleanValue) {
}

RuleTreeNode::RuleTreeNode(const double numberValue)
    : value(numberValue) {
}

RuleTreeNode::RuleTreeNode(std::string textValue)
    : value(std::move(textValue)) {
}

RuleTreeNode::RuleTreeNode(const char* textValue)
    : value(std::string{textValue}) {
}

RuleTreeNode::RuleTreeNode(Object objectValue)
    : value(std::move(objectValue)) {
}

RuleTreeNode::RuleTreeNode(Array arrayValue)
    : value(std::move(arrayValue)) {
}

bool RuleTreeNode::isString() const noexcept {
    return std::holds_alternative<std::string>(value);
}

bool RuleTreeNode::isObject() const noexcept {
    return std::holds_alternative<Object>(value);
}

bool RuleTreeNode::isArray() const noexcept {
    return std::holds_alternative<Array>(value);
}

const std::string& RuleTreeNode::asString() const {
    return std::get<std::string>(value);
}

const RuleTreeNode::Object& RuleTreeNode::asObject() const {
    return std::get<Object>(value);
}

const RuleTreeNode::Array& RuleTreeNode::asArray() const {
    return std::get<Array>(value);
}

bool RulesTreeParseResult::success() const noexcept {
    return errors.empty();
}

namespace {

[[nodiscard]] std::string appendPath(const std::string& base, const std::string& part) {
    if (base.empty()) {
        return part;
    }
    return base + "/" + part;
}

[[nodiscard]] bool isExpressionField(const std::string& fieldName) {
    return fieldName == "check" || fieldName == "value" || fieldName == "time";
}

void parseAtNode(const RuleTreeNode& node, const std::string& path, RulesTreeParseResult* out) {
    if (node.isObject()) {
        for (const auto& [key, child] : node.asObject()) {
            const std::string childPath = appendPath(path, key);
            if (isExpressionField(key) && child.isString()) {
                const ExpressionParseResult parsed = ExpressionParser::parse(child.asString());
                if (!parsed.success) {
                    for (auto error : parsed.errors) {
                        error.sourcePath = childPath;
                        out->errors.push_back(std::move(error));
                    }
                } else {
                    out->snippetsByPath.insert({
                        childPath,
                        ParsedScriptSnippet{.ast = parsed.ast, .externalVariables = parsed.externalVariables}
                    });
                    out->externalVariables.insert(parsed.externalVariables.begin(), parsed.externalVariables.end());
                }
            }
            parseAtNode(child, childPath, out);
        }
        return;
    }

    if (node.isArray()) {
        const auto& values = node.asArray();
        for (std::size_t index = 0U; index < values.size(); ++index) {
            parseAtNode(values[index], appendPath(path, std::to_string(index)), out);
        }
    }
}

} // namespace

RulesTreeParseResult RulesTreeParser::parse(const RuleTreeNode& root) {
    RulesTreeParseResult result;
    parseAtNode(root, "", &result);
    return result;
}

} // namespace yaha
