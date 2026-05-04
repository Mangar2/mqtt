#include "yaha/message_store/message_tree.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace yaha {

namespace {

bool reasonListsEqual(const std::vector<ReasonEntry>& left,
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
}

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

    node->hasData = true;
    node->data.timeMs = nowMilliseconds();
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
MessageTree::getNodes(const std::vector<MessageTreeSnapshotNode>& snapshot) const {
    std::vector<MessageTreeNode> currentNodes{};
    collectAllNodes(root_, currentNodes);

    std::unordered_map<std::string, const MessageTreeSnapshotNode*> snapshotIndex{};
    snapshotIndex.reserve(snapshot.size());
    for (const auto& node : snapshot) {
        snapshotIndex[node.topic] = &node;
    }

    std::vector<MessageTreeNode> diff{};
    for (auto& node : currentNodes) {
        auto iter = snapshotIndex.find(node.topic);
        if (iter == snapshotIndex.end() || !snapshotEquals(node, *iter->second)) {
            node.history.clear();
            diff.push_back(std::move(node));
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
    constexpr std::int64_t k_millisPerDay = 86400000;
    const std::int64_t cutoffMs = nowMilliseconds() -
        (static_cast<std::int64_t>(daysWithoutUpdate) * k_millisPerDay);
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

        auto iter = std::find_if(current->children.begin(), current->children.end(),
                                 [&segment](const auto& child) {
            return child.first == segment;
        });

        if (iter == current->children.end()) {
            TreeNode child{};
            child.topicPath = currentPath;
            current->children.push_back({segment, std::move(child)});
            current = &current->children.back().second;
            continue;
        }

        current = &iter->second;
    }

    return current;
}

const MessageTree::TreeNode* MessageTree::findPath(const std::string& topic) const {
    const TreeNode* current = &root_;
    for (const auto& segment : splitTopic(topic)) {
        const auto iter = std::find_if(current->children.begin(), current->children.end(),
                                       [&segment](const auto& child) {
            return child.first == segment;
        });
        if (iter == current->children.end()) {
            return nullptr;
        }
        current = &iter->second;
    }
    return current;
}

void MessageTree::appendHistory(NodeData& data) const {
    MessageTreeHistoryEntry entry{};
    entry.timeMs = data.timeMs;
    entry.value = data.value;
    entry.reason = data.reason;

    if (!data.compressedHistory.empty()) {
        auto& last = data.compressedHistory.back();
        if (last.entry.value == entry.value &&
            reasonListsEqual(last.entry.reason, entry.reason) &&
            last.repeatCount < config_.maxValuesPerHistoryEntry) {
            last.repeatCount += 1U;
            last.entry.timeMs = entry.timeMs;
            return;
        }
    }

    data.compressedHistory.push_back({std::move(entry), 1U});
}

void MessageTree::trimHistory(NodeData& data) const {
    std::uint32_t totalCount = 0U;
    for (const auto& item : data.compressedHistory) {
        totalCount += item.repeatCount;
    }

    if (totalCount <= config_.maxHistoryLength) {
        return;
    }

    const std::uint32_t trimTarget = config_.maxHistoryLength - config_.historyHysterese;
    while (totalCount > trimTarget && !data.compressedHistory.empty()) {
        auto& front = data.compressedHistory.front();
        if (front.repeatCount > 1U) {
            front.repeatCount -= 1U;
            totalCount -= 1U;
            continue;
        }
        data.compressedHistory.erase(data.compressedHistory.begin());
        totalCount -= 1U;
    }
}

std::vector<std::string> MessageTree::splitTopic(const std::string& topic) {
    if (topic.empty()) {
        return {};
    }

    std::vector<std::string> parts{};
    std::string current{};
    for (const char chr : topic) {
        if (chr == '/') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(chr);
    }
    parts.push_back(current);
    return parts;
}

std::vector<MessageTreeHistoryEntry>
MessageTree::decompressHistory(const std::vector<CompressedHistoryEntry>& compressed,
                               bool includeReason) {
    std::vector<MessageTreeHistoryEntry> history{};
    for (const auto& item : compressed) {
        for (std::uint32_t idx = 0U; idx < item.repeatCount; ++idx) {
            MessageTreeHistoryEntry entry = item.entry;
            if (!includeReason) {
                entry.reason.clear();
            }
            history.push_back(std::move(entry));
        }
    }
    return history;
}

std::vector<MessageTree::CompressedHistoryEntry>
MessageTree::compressHistory(const std::vector<MessageTreeHistoryEntry>& history) const {
    std::vector<CompressedHistoryEntry> compressed{};
    for (const auto& entry : history) {
        if (!compressed.empty()) {
            auto& last = compressed.back();
            if (last.entry.value == entry.value &&
                reasonListsEqual(last.entry.reason, entry.reason) &&
                last.repeatCount < config_.maxValuesPerHistoryEntry) {
                last.repeatCount += 1U;
                last.entry.timeMs = entry.timeMs;
                continue;
            }
        }

        compressed.push_back({entry, 1U});
    }

    return compressed;
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
            ? decompressHistory(node.data.compressedHistory, includeReason)
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

void MessageTree::collectAllNodes(const TreeNode& node,
                                  std::vector<MessageTreeNode>& output) const {
    if (node.hasData) {
        MessageTreeNode result{};
        result.topic = node.topicPath;
        result.timeMs = node.data.timeMs;
        result.value = node.data.value;
        result.reason = node.data.reason;
        output.push_back(std::move(result));
    }

    for (const auto& child : node.children) {
        collectAllNodes(child.second, output);
    }
}

bool MessageTree::snapshotEquals(const MessageTreeNode& current,
                                 const MessageTreeSnapshotNode& snapshot) {
    return current.topic == snapshot.topic &&
           current.value == snapshot.value &&
        reasonListsEqual(current.reason, snapshot.reason);
}

std::size_t MessageTree::cleanupNode(TreeNode& node, std::int64_t cutoffMs) {
    std::size_t removed = 0U;

    for (auto iter = node.children.begin(); iter != node.children.end();) {
        removed += cleanupNode(iter->second, cutoffMs);
        if (!iter->second.hasData && iter->second.children.empty()) {
            iter = node.children.erase(iter);
            continue;
        }
        ++iter;
    }

    if (node.hasData && node.data.timeMs < cutoffMs) {
        node.hasData = false;
        node.data = NodeData{};
        removed += 1U;
    }

    return removed;
}

} // namespace yaha
