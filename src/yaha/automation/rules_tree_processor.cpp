#include "yaha/automation/rules_tree_processor.h"

#include <string>

#include "yaha/automation/single_rule_processor.h"

namespace yaha {
namespace {

[[nodiscard]] std::string appendPathSegment(const std::string& basePath, const std::string& segment) {
    if (basePath.empty()) {
        return segment;
    }
    return basePath + "/" + segment;
}

[[nodiscard]] bool isRuleObjectNode(const RuleTreeNode& node) {
    return node.isObject() && node.asObject().contains("topic");
}

void processNodeRecursively(
    const RuleTreeNode& node,
    const std::string& nodePath,
    const ExpressionEvaluator::VariableMap& variables,
    RulesTreeProcessingResult* result) {
    if (isRuleObjectNode(node)) {
        result->processedRules += 1U;

        const SingleRuleProcessingResult ruleResult = SingleRuleProcessor::process(node, variables);
        result->usedVariables.insert(ruleResult.usedVariables.begin(), ruleResult.usedVariables.end());

        if (!ruleResult.success) {
            const std::string displayPath = nodePath.empty() ? std::string{"<root>"} : nodePath;
            for (const auto& errorText : ruleResult.errors) {
                std::string formattedError = displayPath;
                formattedError.append(": ");
                formattedError.append(errorText);
                result->errors.push_back(std::move(formattedError));
            }
        } else if (ruleResult.triggered && ruleResult.message.has_value()) {
            result->triggeredRules += 1U;
            result->messages.push_back(ruleResult.message.value().clone());
        }
    }

    if (node.isObject()) {
        for (const auto& [fieldName, childNode] : node.asObject()) {
            processNodeRecursively(childNode, appendPathSegment(nodePath, fieldName), variables, result);
        }
        return;
    }

    if (node.isArray()) {
        const auto& elements = node.asArray();
        for (std::size_t index = 0U; index < elements.size(); ++index) {
            processNodeRecursively(elements[index], appendPathSegment(nodePath, std::to_string(index)), variables, result);
        }
    }
}

} // namespace

RulesTreeProcessingResult RulesTreeProcessor::process(
    const RuleTreeNode& root,
    const ExpressionEvaluator::VariableMap& variables) {
    RulesTreeProcessingResult result;
    processNodeRecursively(root, "", variables, &result);
    result.success = result.errors.empty();
    return result;
}

} // namespace yaha
