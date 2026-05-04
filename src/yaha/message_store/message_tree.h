#pragma once

/**
 * @file message_tree.h
 * @brief MessageTree data structure for MessageStore state and history.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yaha {

/**
 * @brief One historic value entry for a topic node.
 */
struct MessageTreeHistoryEntry {
    std::int64_t timeMs{0};               ///< Wall-clock timestamp of this historic state.
    Value value{std::string{}};           ///< Historic value.
    std::vector<ReasonEntry> reason;      ///< Historic reason chain.
};

/**
 * @brief Public node representation returned by tree queries.
 */
struct MessageTreeNode {
    std::string topic;                             ///< Full topic path.
    std::int64_t timeMs{0};                        ///< Wall-clock timestamp of current value.
    Value value{std::string{}};                    ///< Current value.
    std::vector<ReasonEntry> reason;               ///< Current reason chain.
    std::vector<MessageTreeHistoryEntry> history;  ///< Decompressed history entries.
};

/**
 * @brief Snapshot node used by diff query mode.
 */
struct MessageTreeSnapshotNode {
    std::string topic;                          ///< Full topic path.
    Value value{std::string{}};                 ///< Snapshot value.
    std::vector<ReasonEntry> reason;            ///< Snapshot reason chain.
};

/**
 * @brief Runtime configuration for MessageTree behavior.
 */
struct MessageTreeConfig {
    static constexpr std::uint32_t k_default_max_history_length{50U};
    static constexpr std::uint32_t k_default_history_hysterese{10U};
    static constexpr std::uint32_t k_default_max_values_per_history_entry{256U};

    std::uint32_t maxHistoryLength{k_default_max_history_length}; ///< Hard limit for decompressed history entries.
    std::uint32_t historyHysterese{k_default_history_hysterese}; ///< Batch trim amount once max is exceeded.
    std::uint32_t maxValuesPerHistoryEntry{k_default_max_values_per_history_entry}; ///< Max repeat compression count per bucket.
    std::function<std::int64_t()> nowMillisecondsProvider; ///< Time source override for tests.
};

/**
 * @brief Topic-segment tree for current values, history, and cleanup queries.
 */
class MessageTree {
public:
    /**
     * @brief Constructs tree with configuration.
     * @param config Runtime behavior configuration.
    */
    explicit MessageTree(MessageTreeConfig config = {});

    /**
     * @brief Inserts or updates a topic node.
     * @param message Input message.
     */
    void addData(const Message& message);

    /**
     * @brief Returns flat nodes under prefix up to relative depth.
     * @param topicPrefix Query prefix; empty means root.
     * @param levelAmount Relative depth from prefix.
     * @param includeHistory Include decompressed history in result.
     * @param includeReason Include reason arrays in result.
     * @return Flat list of nodes.
     */
    [[nodiscard]] std::vector<MessageTreeNode>
    getSection(const std::string& topicPrefix,
               std::uint32_t levelAmount,
               bool includeHistory,
               bool includeReason) const;

    /**
     * @brief Returns nodes that differ from a provided snapshot.
     * @param snapshot Prior state snapshot.
     * @return Changed or new nodes.
     */
    [[nodiscard]] std::vector<MessageTreeNode>
    getNodes(const std::vector<MessageTreeSnapshotNode>& snapshot) const;

    /**
     * @brief Replaces full tree content with provided nodes.
     * @param nodes Full snapshot nodes to import.
     */
    void replaceAllNodes(const std::vector<MessageTreeNode>& nodes);

    /**
     * @brief Removes nodes older than the provided day threshold.
     * @param daysWithoutUpdate Age threshold in days.
     * @return Number of removed data nodes.
     */
    std::size_t cleanup(std::uint32_t daysWithoutUpdate);

private:
    /**
     * @brief Compressed internal history bucket.
     */
    struct CompressedHistoryEntry {
        MessageTreeHistoryEntry entry{};     ///< Representative history entry.
        std::uint32_t repeatCount{1U};       ///< Number of equal consecutive entries.
    };

    /**
     * @brief Internal payload state of one data node.
     */
    struct NodeData {
        std::int64_t timeMs{0};                               ///< Current timestamp.
        Value value{std::string{}};                           ///< Current value.
        std::vector<ReasonEntry> reason;                      ///< Current reason.
        std::vector<CompressedHistoryEntry> compressedHistory; ///< Compressed historic values.
    };

    /**
     * @brief Internal topic-segment node.
     */
    struct TreeNode {
        std::string topicPath;                                ///< Full topic path for this node.
        std::vector<std::pair<std::string, TreeNode>> children; ///< Child segments.
        bool hasData{false};                                  ///< True when current data is present.
        NodeData data{};                                      ///< Current data payload.
    };

    /**
     * @brief Returns current wall-clock milliseconds.
     * @return Current timestamp in milliseconds.
     */
    [[nodiscard]] std::int64_t nowMilliseconds() const;

    /**
     * @brief Creates all missing nodes for a topic path.
     * @param topic Full topic path.
     * @return Pointer to final path node.
     */
    [[nodiscard]] TreeNode* ensurePath(const std::string& topic);

    /**
     * @brief Finds existing node for topic path.
     * @param topic Full topic path.
     * @return Pointer to path node or null.
     */
    [[nodiscard]] const TreeNode* findPath(const std::string& topic) const;

    /**
     * @brief Appends current node value as compressed history entry.
     * @param data Mutable node data.
     */
    void appendHistory(NodeData& data) const;

    /**
     * @brief Applies bounded-history trimming with hysteresis.
     * @param data Mutable node data.
     */
    void trimHistory(NodeData& data) const;

    /**
     * @brief Splits topic by slash into path segments.
     * @param topic Full topic string.
     * @return Topic segments.
     */
    [[nodiscard]] static std::vector<std::string> splitTopic(const std::string& topic);

    /**
     * @brief Expands compressed history for API output.
     * @param compressed Internal compressed history.
     * @param includeReason Include reason in expanded entries.
     * @return Decompressed history list.
     */
    [[nodiscard]] static std::vector<MessageTreeHistoryEntry>
    decompressHistory(const std::vector<CompressedHistoryEntry>& compressed,
                      bool includeReason);

    [[nodiscard]] std::vector<CompressedHistoryEntry>
    compressHistory(const std::vector<MessageTreeHistoryEntry>& history) const;

    /**
     * @brief Traverses subtree and appends section query nodes.
     * @param node Current traversal node.
     * @param maxDepth Maximum relative depth.
     * @param currentDepth Current relative depth.
     * @param includeHistory Include history output.
     * @param includeReason Include reason output.
     * @param output Result sink.
     */
    void collectSection(const TreeNode& node,
                        std::uint32_t maxDepth,
                        std::uint32_t currentDepth,
                        bool includeHistory,
                        bool includeReason,
                        std::vector<MessageTreeNode>& output) const;

    /**
     * @brief Collects all current data nodes in full tree.
     * @param node Current traversal node.
     * @param output Result sink.
     */
    void collectAllNodes(const TreeNode& node,
                         std::vector<MessageTreeNode>& output) const;

    /**
     * @brief Compares current node with snapshot node for diff mode.
     * @param current Current node.
     * @param snapshot Snapshot node.
     * @return True when equivalent for topic/value/reason.
     */
    [[nodiscard]] static bool snapshotEquals(const MessageTreeNode& current,
                                             const MessageTreeSnapshotNode& snapshot);

    /**
     * @brief Recursively removes stale data and prunes empty branches.
     * @param node Current subtree node.
     * @param cutoffMs Remove nodes older than this timestamp.
     * @return Number of removed data nodes in subtree.
     */
    std::size_t cleanupNode(TreeNode& node, std::int64_t cutoffMs);

    MessageTreeConfig config_{};
    TreeNode root_{};
};

} // namespace yaha
