#include "yaha/automation_client/automation_control_topics.h"

#include <algorithm>
#include <cctype>

namespace yaha::automation_control_topics {
namespace {

constexpr std::size_t k_management_suffix_length{4U};
constexpr std::size_t k_debug_suffix_length{6U};

} // namespace

bool startsWithText(const std::string& textValue, const std::string& prefix) {
    return textValue.starts_with(prefix);
}

bool endsWithSetSuffix(const std::string& textValue) {
    return textValue.size() >= k_management_suffix_length
        && textValue.compare(textValue.size() - k_management_suffix_length, k_management_suffix_length, "/set") == 0;
}

bool endsWithDebugSuffix(const std::string& textValue) {
    return textValue.size() >= k_debug_suffix_length
        && textValue.compare(textValue.size() - k_debug_suffix_length, k_debug_suffix_length, "/debug") == 0;
}

bool isDeletePayloadText(const std::string& payload) {
    std::string trimmed = payload;
    trimmed.erase(trimmed.begin(), std::ranges::find_if(trimmed, [](unsigned char currentChar) {
        return std::isspace(currentChar) == 0;
    }));
    trimmed.erase(std::ranges::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char currentChar) {
        return std::isspace(currentChar) == 0;
    }).base(), trimmed.end());
    return trimmed == "delete";
}

bool isMonitoringTopic(const std::string& topicName, const std::string& monitorTopicPrefix) {
    return startsWithText(topicName, monitorTopicPrefix + "/");
}

bool isManagementTopic(const std::string& topicName, const std::string& managementTopicPrefix) {
    return startsWithText(topicName, managementTopicPrefix + "/") && endsWithSetSuffix(topicName);
}

bool isDebugTopic(const std::string& topicName, const std::string& debugTopicPrefix) {
    return startsWithText(topicName, debugTopicPrefix) && endsWithDebugSuffix(topicName);
}

std::optional<std::string> extractRuleNameFromManagementTopic(
    const std::string& topicName,
    const std::string& managementTopicPrefix) {
    const std::string prefix = managementTopicPrefix + "/";
    if (!startsWithText(topicName, prefix) || !endsWithSetSuffix(topicName)) {
        return std::nullopt;
    }

    const std::size_t startIndex = prefix.size();
    const std::size_t endIndex = topicName.size() - k_management_suffix_length;
    if (endIndex <= startIndex) {
        return std::nullopt;
    }

    return topicName.substr(startIndex, endIndex - startIndex);
}

std::optional<std::string> extractRuleLinkFromDebugTopic(
    const std::string& topicName,
    const std::string& debugTopicPrefix) {
    if (!startsWithText(topicName, debugTopicPrefix) || !endsWithDebugSuffix(topicName)) {
        return std::nullopt;
    }

    const std::size_t startIndex = debugTopicPrefix.size();
    const std::size_t endIndex = topicName.size() - k_debug_suffix_length;
    if (endIndex <= startIndex) {
        return std::nullopt;
    }

    const std::string ruleLink = topicName.substr(startIndex, endIndex - startIndex);
    if (ruleLink.empty()) {
        return std::nullopt;
    }
    return std::string{ruleLink};
}

std::string buildTraceTopicFromRuleLink(
    const std::string& debugTopicPrefix,
    const std::string& ruleLink) {
    return debugTopicPrefix + ruleLink + "/trace";
}

} // namespace yaha::automation_control_topics
