#include "yaha/message_store/message_tree.h"

#include "yaha/message_store/iso_timestamp_parser.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::int64_t k_millis_per_day{86400000};
constexpr std::uint32_t k_legacy_length_for_further_compression_minimum{3U};

[[nodiscard]] bool reasonListsEqual(const std::vector<ReasonEntry>& left,
                                    const std::vector<ReasonEntry>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t idx = 0U; idx < left.size(); ++idx) {
        if (left[idx].message != right[idx].message ||
            left[idx].timestamp != right[idx].timestamp) {
            return false;
        }
    }

    return true;
}

} // namespace

MessageTree::MessageTree(MessageTreeConfig config)
    : config_(std::move(config)) {
    if (!config_.nowMillisecondsProvider) {
        config_.nowMillisecondsProvider = []() {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        };
    }

    if (config_.maxHistoryLength == 0U) {
        throw std::invalid_argument{"MessageTree maxHistoryLength must be > 0"};
    }

    if (config_.historyHysterese > config_.maxHistoryLength) {
        throw std::invalid_argument{"MessageTree historyHysterese must be <= maxHistoryLength"};
    }

    if (config_.lengthForFurtherCompression == 1U ||
        config_.lengthForFurtherCompression == 2U) {
        config_.lengthForFurtherCompression = k_legacy_length_for_further_compression_minimum;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MessageTree::addData(const Message& message) {
    Message::validate(message);

    TreeNode* node = ensurePath(message.topic());
    if (node == nullptr) {
        throw std::runtime_error{"MessageTree failed to create topic path"};
    }

    if (node->hasData) {
        appendHistory(node->data);
        trimHistory(node->data);
    }

    std::int64_t effectiveTimeMs = nowMilliseconds();
    const auto& reasons = message.reason();
    if (!reasons.empty()) {
        std::int64_t reasonTimeMs = 0;
        if (tryParseIsoTimestampMilliseconds(reasons.front().timestamp, reasonTimeMs)) {
            effectiveTimeMs = reasonTimeMs;

            if (node->hasData && effectiveTimeMs <= node->data.timeMs) {
                std::int64_t newestReasonTimeMs = effectiveTimeMs;
                for (std::size_t idx = 1U; idx < reasons.size(); ++idx) {
                    std::int64_t candidateTimeMs = 0;
                    if (!tryParseIsoTimestampMilliseconds(reasons[idx].timestamp, candidateTimeMs)) {
                        continue;
                    }
                    newestReasonTimeMs = std::max(newestReasonTimeMs, candidateTimeMs);
                }
                if (newestReasonTimeMs > node->data.timeMs) {
                    effectiveTimeMs = newestReasonTimeMs;
                }
            }
        }
    }
    node->hasData = true;

    node->data.timeMs = effectiveTimeMs;
    node->data.value = message.value();
    node->data.reason = message.reason();
}

std::vector<MessageTreeNode>
MessageTree::getSection(const std::string& topicPrefix,
                        std::uint32_t levelAmount,
                        bool includeHistory,
                        bool includeReason) const {
    const TreeNode* start = topicPrefix.empty() ? &root_ : findPath(topicPrefix);
    if (start == nullptr) {
        return {};
    }

    std::vector<MessageTreeNode> output{};
    collectSection(*start, levelAmount, 0U, includeHistory, includeReason, output);
    return output;
}

std::vector<MessageTreeNode>
MessageTree::getNodes(const std::vector<MessageTreeSnapshotNode>& snapshot,
                      bool includeHistory,
                      bool includeReason) const {
    std::vector<MessageTreeNode> diff{};
    diff.reserve(snapshot.size());
    for (const auto& requiredNode : snapshot) {
        const TreeNode* current = findPath(requiredNode.topic);
        if (current == nullptr || !current->hasData) {
            continue;
        }

        MessageTreeNode currentNode{};
        currentNode.topic = current->topicPath;
        currentNode.timeMs = current->data.timeMs;
        currentNode.value = current->data.value;
        currentNode.reason = current->data.reason;
        currentNode.history = includeHistory
            ? decompressHistory(current->data.compressedHistory, true)
            : std::vector<MessageTreeHistoryEntry>{};

        if (!snapshotEquals(currentNode, requiredNode)) {
            if (!includeReason) {
                currentNode.reason.clear();
            }
            diff.push_back(std::move(currentNode));
        }
    }

    return diff;
}

void MessageTree::replaceAllNodes(const std::vector<MessageTreeNode>& nodes) {
    root_ = TreeNode{};

    for (const auto& node : nodes) {
        if (node.topic.empty()) {
            continue;
        }

        TreeNode* target = ensurePath(node.topic);
        if (target == nullptr) {
            continue;
        }

        target->hasData = true;
        target->data.timeMs = node.timeMs;
        target->data.value = node.value;
        target->data.reason = node.reason;
        target->data.compressedHistory = compressHistory(node.history);
        trimHistory(target->data);
    }
}

std::size_t MessageTree::cleanup(std::uint32_t daysWithoutUpdate) {
    const std::int64_t cutoffMs = nowMilliseconds() -
        (static_cast<std::int64_t>(daysWithoutUpdate) * k_millis_per_day);
    return cleanupNode(root_, cutoffMs);
}

std::int64_t MessageTree::nowMilliseconds() const {
    return config_.nowMillisecondsProvider();
}

MessageTree::TreeNode* MessageTree::ensurePath(const std::string& topic) {
    TreeNode* current = &root_;
    std::string currentPath{};

    for (const auto& segment : splitTopic(topic)) {
        if (!currentPath.empty()) {
            currentPath.push_back('/');
        }
        currentPath += segment;

        const auto lookupIter = current->childLookup.find(segment);
        if (lookupIter == current->childLookup.end()) {
            TreeNode child{};
            child.topicPath = currentPath;
            current->children.emplace_back(segment, std::move(child));
            const std::size_t newIndex = current->children.size() - 1U;
            current->childLookup.emplace(current->children.back().first, newIndex);
            current = &current->children[newIndex].second;
            continue;
        }

        current = &current->children[lookupIter->second].second;
    }

    return current;
}

const MessageTree::TreeNode* MessageTree::findPath(const std::string& topic) const {
    const TreeNode* current = &root_;
    for (const auto& segment : splitTopic(topic)) {
        const auto lookupIter = current->childLookup.find(segment);
        if (lookupIter == current->childLookup.end()) {
            return nullptr;
        }
        current = &current->children[lookupIter->second].second;
    }
    return current;
}

std::vector<std::string> MessageTree::splitTopic(const std::string& topic) {
    if (topic.empty()) {
        return {};
    }

    std::vector<std::string> parts{};
    parts.reserve(static_cast<std::size_t>(
        std::count(topic.begin(), topic.end(), '/')) + 1U);

    std::size_t start = 0U;
    while (start <= topic.size()) {
        const std::size_t slashPos = topic.find('/', start);
        if (slashPos == std::string::npos) {
            parts.emplace_back(topic.substr(start));
            break;
        }
        parts.emplace_back(topic.substr(start, slashPos - start));
        start = slashPos + 1U;
    }

    return parts;
}

void MessageTree::rebuildChildLookup(TreeNode& node) {
    node.childLookup.clear();
    node.childLookup.reserve(node.children.size());
    for (std::size_t index = 0U; index < node.children.size(); ++index) {
        node.childLookup.emplace(node.children[index].first, index);
    }
}

void MessageTree::collectSection(const TreeNode& node,
                                 std::uint32_t maxDepth,
                                 std::uint32_t currentDepth,
                                 bool includeHistory,
                                 bool includeReason,
                                 std::vector<MessageTreeNode>& output) const {
    if (node.hasData) {
        MessageTreeNode result{};
        result.topic = node.topicPath;
        result.timeMs = node.data.timeMs;
        result.value = node.data.value;
        result.reason = includeReason ? node.data.reason : std::vector<ReasonEntry>{};
        result.history = includeHistory
            ? decompressHistory(node.data.compressedHistory, true)
            : std::vector<MessageTreeHistoryEntry>{};
        output.push_back(std::move(result));
    }

    if (currentDepth >= maxDepth) {
        return;
    }

    for (const auto& child : node.children) {
        collectSection(child.second,
                       maxDepth,
                       currentDepth + 1U,
                       includeHistory,
                       includeReason,
                       output);
    }
}

bool MessageTree::snapshotEquals(const MessageTreeNode& current,
                                 const MessageTreeSnapshotNode& snapshot) {
    if (snapshot.timeMs.has_value() && current.timeMs != *snapshot.timeMs) {
        return false;
    }

    if (current.value != snapshot.value) {
        return false;
    }

    if (!snapshot.hasReason) {
        return true;
    }

    return reasonListsEqual(current.reason, snapshot.reason);
}

std::size_t MessageTree::cleanupNode(TreeNode& node, std::int64_t cutoffMs) {
    std::size_t removed = 0U;
    bool erasedChild = false;

    for (auto iter = node.children.begin(); iter != node.children.end();) {
        removed += cleanupNode(iter->second, cutoffMs);
        if (!iter->second.hasData && iter->second.children.empty()) {
            iter = node.children.erase(iter);
            erasedChild = true;
            continue;
        }
        ++iter;
    }

    if (erasedChild) {
        rebuildChildLookup(node);
    }

    if (node.hasData && node.data.timeMs < cutoffMs) {
        node.hasData = false;
        node.data = NodeData{};
        removed += 1U;
    }

    return removed;
}

} // namespace yaha
