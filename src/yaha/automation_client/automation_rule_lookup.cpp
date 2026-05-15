#include "yaha/automation_client/automation_rule_lookup.h"

#include <algorithm>

namespace yaha::automation_rule_lookup {
namespace {

void appendCandidatePath(std::vector<std::vector<std::string>>* candidates,
                         const std::vector<std::string>& candidatePath) {
    if (candidatePath.empty()) {
        return;
    }
    if (std::ranges::find(*candidates, candidatePath) == candidates->end()) {
        candidates->push_back(candidatePath);
    }
}

} // namespace

std::vector<std::string> splitPathSegments(const std::string& pathText) {
    std::vector<std::string> segments{};
    std::string currentSegment{};
    for (const char currentChar : pathText) {
        if (currentChar == '/') {
            if (!currentSegment.empty()) {
                segments.push_back(currentSegment);
                currentSegment.clear();
            }
            continue;
        }
        currentSegment.push_back(currentChar);
    }
    if (!currentSegment.empty()) {
        segments.push_back(currentSegment);
    }
    return segments;
}

std::string joinPathSegments(const std::vector<std::string>& segments) {
    std::string pathText{};
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        if (index > 0U) {
            pathText.push_back('/');
        }
        pathText.append(segments[index]);
    }
    return pathText;
}

std::vector<std::vector<std::string>> buildRuleLookupCandidates(
    const std::vector<std::string>& ruleSegments) {
    std::vector<std::vector<std::string>> candidates{};
    if (ruleSegments.empty()) {
        return candidates;
    }

    std::vector<std::string> normalizedSegments = ruleSegments;
    if (!normalizedSegments.empty() && normalizedSegments.front() == "rules") {
        normalizedSegments.erase(normalizedSegments.begin());
    }
    if (normalizedSegments.empty()) {
        return candidates;
    }

    appendCandidatePath(&candidates, normalizedSegments);

    std::vector<std::string> rootRulesPath{"rules"};
    rootRulesPath.insert(rootRulesPath.end(), normalizedSegments.begin(), normalizedSegments.end());
    appendCandidatePath(&candidates, rootRulesPath);

    if (normalizedSegments.size() >= 2U) {
        std::vector<std::string> implicitRulesPath = normalizedSegments;
        implicitRulesPath.insert(implicitRulesPath.end() - 1, "rules");
        appendCandidatePath(&candidates, implicitRulesPath);

        std::vector<std::string> rootAndImplicitRulesPath{"rules"};
        rootAndImplicitRulesPath.insert(
            rootAndImplicitRulesPath.end(), implicitRulesPath.begin(), implicitRulesPath.end());
        appendCandidatePath(&candidates, rootAndImplicitRulesPath);
    }

    return candidates;
}

std::optional<RuleTreeNode> findNodeByPathSegments(
    const RuleTreeNode& rootNode,
    const std::vector<std::string>& segments) {
    const RuleTreeNode* currentNode = &rootNode;
    for (const auto& segment : segments) {
        if (!currentNode->isObject()) {
            return std::nullopt;
        }

        const auto& objectNode = currentNode->asObject();
        const auto iterator = objectNode.find(segment);
        if (iterator == objectNode.end()) {
            return std::nullopt;
        }

        currentNode = &iterator->second;
    }

    return *currentNode;
}

} // namespace yaha::automation_rule_lookup
