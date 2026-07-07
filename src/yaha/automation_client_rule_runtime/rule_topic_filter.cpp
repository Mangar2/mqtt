#include "yaha/automation_client_rule_runtime/rule_topic_filter.h"

#include <algorithm>

namespace yaha {

std::string joinPath(const std::string& basePath, const std::string& segment) {
    if (basePath.empty()) {
        return segment;
    }
    return basePath + "/" + segment;
}

bool isRuleNode(const RuleTreeNode& node) {
    return node.isObject() && node.asObject().contains("topic");
}

bool topicShapeValid(const RuleTreeNode& topicNode) {
    if (topicNode.isString()) {
        return !topicNode.asString().empty();
    }

    if (topicNode.isArray()) {
        const auto& topicArray = topicNode.asArray();
        if (topicArray.empty()) {
            return false;
        }
        return std::ranges::all_of(topicArray, [](const RuleTreeNode& entryNode) {
            return entryNode.isString() && !entryNode.asString().empty();
        });
    }

    if (topicNode.isObject()) {
        const auto& topicMap = topicNode.asObject();
        if (topicMap.empty()) {
            return false;
        }
        return std::ranges::all_of(topicMap, [](const auto& mapEntry) {
            return !mapEntry.first.empty();
        });
    }

    return false;
}

bool runtimeMatchesTopicFilter(const std::string_view topicFilter, const std::string_view topicName) {
    std::size_t filterPos = 0U;
    std::size_t topicPos = 0U;

    while (filterPos < topicFilter.size()) {
        if (topicPos > topicName.size()) {
            return false;
        }

        const std::size_t filterEnd = topicFilter.find('/', filterPos);
        const std::size_t topicEnd = topicName.find('/', topicPos);

        const std::string_view filterSegment = filterEnd == std::string_view::npos
            ? topicFilter.substr(filterPos)
            : topicFilter.substr(filterPos, filterEnd - filterPos);
        std::string_view topicSegment{};
        if (topicPos <= topicName.size()) {
            if (topicEnd == std::string_view::npos) {
                topicSegment = topicName.substr(topicPos);
            } else {
                topicSegment = topicName.substr(topicPos, topicEnd - topicPos);
            }
        }

        if (filterSegment == "#") {
            return true;
        }

        if (topicPos >= topicName.size()) {
            return false;
        }

        if (filterSegment != "+" && filterSegment != topicSegment) {
            return false;
        }

        if (filterEnd == std::string_view::npos) {
            return topicEnd == std::string_view::npos;
        }

        if (topicEnd == std::string_view::npos) {
            return false;
        }

        filterPos = filterEnd + 1U;
        topicPos = topicEnd + 1U;
    }

    return topicPos == topicName.size();
}

bool isMotionTopic(
    const std::string& topicName,
    const std::vector<std::string>& motionTopicFilters) {
    return std::ranges::any_of(motionTopicFilters, [&topicName](const std::string& topicFilter) {
        return runtimeMatchesTopicFilter(topicFilter, topicName);
    });
}

} // namespace yaha
