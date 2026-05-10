#pragma once

/**
 * @file message_tree.h
 * @brief MessageTree data structure for MessageStore state and history.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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
    bool hasReason{false};                      ///< True when reason field was explicitly provided.
    std::optional<std::int64_t> timeMs;         ///< Optional snapshot timestamp for time-aware diff.
};

/**
 * @brief Runtime configuration for MessageTree behavior.
 */
struct MessageTreeConfig {
    static constexpr std::uint32_t k_default_max_history_length{50U};
    static constexpr std::uint32_t k_default_history_hysterese{10U};
    static constexpr std::uint32_t k_default_max_values_per_history_entry{256U};
    static constexpr std::uint32_t k_default_length_for_further_compression{10U};
    static constexpr double k_default_upper_bound_factor{1.2};
    static constexpr std::uint32_t k_default_upper_bound_add_in_milliseconds{1000U};
    static constexpr double k_default_lower_bound_factor{0.8};
    static constexpr std::uint32_t k_default_lower_bound_sub_in_milliseconds{1000U};

    std::uint32_t maxHistoryLength{k_default_max_history_length}; ///< Hard limit for decompressed history entries.
    std::uint32_t historyHysterese{k_default_history_hysterese}; ///< Batch trim amount once max is exceeded.
    std::uint32_t maxValuesPerHistoryEntry{k_default_max_values_per_history_entry}; ///< Max repeat compression count per bucket.
    std::uint32_t lengthForFurtherCompression{k_default_length_for_further_compression}; ///< Threshold for converting time/timeValue entries to interval form.
    double upperBoundFactor{k_default_upper_bound_factor}; ///< Factor used for upper interval bound matching.
    std::uint32_t upperBoundAddInMilliseconds{k_default_upper_bound_add_in_milliseconds}; ///< Constant used for upper interval bound matching.
    double lowerBoundFactor{k_default_lower_bound_factor}; ///< Factor used for lower interval bound matching.
    std::uint32_t lowerBoundSubInMilliseconds{k_default_lower_bound_sub_in_milliseconds}; ///< Constant used for lower interval bound matching.
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
    getNodes(const std::vector<MessageTreeSnapshotNode>& snapshot,
             bool includeHistory,
             bool includeReason) const;

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
     * @brief Compressed entry with exactly one historic value.
     */
    struct SingleHistoryEntry {
        MessageTreeHistoryEntry entry{}; ///< Stored history entry.
    };

    /**
     * @brief Compressed entry with multiple values sharing one reason chain.
     */
    struct TimeValueHistoryEntry {
        std::vector<std::pair<std::int64_t, Value>> values; ///< Ordered oldest-to-newest time/value pairs.
        std::vector<ReasonEntry> reason; ///< Reason chain of the oldest element.
    };

    /**
     * @brief Compressed entry with multiple timestamps sharing one value and reason chain.
     */
    struct TimeHistoryEntry {
        Value value{std::string{}}; ///< Shared value of all timestamps.
        std::vector<std::int64_t> timestamps; ///< Ordered oldest-to-newest timestamps.
        std::vector<ReasonEntry> reason; ///< Reason chain of the oldest element.
    };

    /**
     * @brief Compressed entry representing regular updates as one interval block.
     */
    struct IntervalHistoryEntry {
        std::uint32_t amount{0U}; ///< Amount of compressed entries in this block.
        Value value{std::string{}}; ///< Shared value of the interval block.
        std::vector<ReasonEntry> reason; ///< Reason chain of the oldest element.
        std::int64_t firstTimeMs{0}; ///< Oldest timestamp in the block.
        std::int64_t lastTimeMs{0}; ///< Newest timestamp in the block.
    };

    /**
     * @brief Compressed internal history bucket.
     */
    struct CompressedHistoryEntry {
        std::variant<SingleHistoryEntry,
                     TimeValueHistoryEntry,
                     TimeHistoryEntry,
                     IntervalHistoryEntry> data{}; ///< Type-specific compressed representation.
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
     * @brief Adds one history entry using the original MessageTree compression pipeline.
     * @param history Mutable compressed history list.
     * @param entryToAdd New entry to add as newest history item.
     */
    void addHistoryEntry(std::vector<CompressedHistoryEntry>& history,
                         const MessageTreeHistoryEntry& entryToAdd) const;

    /**
     * @brief Returns whether two reason chains are compression-compatible.
     * @param left Left reason chain.
     * @param right Right reason chain.
     * @return True when both chains have equal message texts.
     */
    [[nodiscard]] static bool areReasonMessagesEqual(const std::vector<ReasonEntry>& left,
                                                     const std::vector<ReasonEntry>& right);

    /**
     * @brief Returns reason chain associated with one compressed history entry.
     * @param entry Compressed history entry.
     * @return Associated reason chain.
     */
    [[nodiscard]] static const std::vector<ReasonEntry>& reasonOf(const CompressedHistoryEntry& entry);

    /**
     * @brief Extends or transforms a newest timeValue compressed entry with a new value.
     * @param newest Mutable newest compressed entry.
     * @param history Mutable full compressed history list.
     * @param entryToAdd New history entry to integrate.
     */
    void addOrConvertTimeValueEntry(CompressedHistoryEntry& newest,
                                    std::vector<CompressedHistoryEntry>& history,
                                    const MessageTreeHistoryEntry& entryToAdd) const;

    /**
     * @brief Extends or transforms a newest time compressed entry with a new value.
     * @param newest Mutable newest compressed entry.
     * @param history Mutable full compressed history list.
     * @param entryToAdd New history entry to integrate.
     */
    void addOrConvertTimeEntry(CompressedHistoryEntry& newest,
                               std::vector<CompressedHistoryEntry>& history,
                               const MessageTreeHistoryEntry& entryToAdd) const;

    /**
     * @brief Extends or replaces a newest interval compressed entry with a new value.
     * @param newest Mutable newest compressed entry.
     * @param history Mutable full compressed history list.
     * @param entryToAdd New history entry to integrate.
     */
    void addOrConvertIntervalEntry(CompressedHistoryEntry& newest,
                                   std::vector<CompressedHistoryEntry>& history,
                                   const MessageTreeHistoryEntry& entryToAdd) const;

    /**
     * @brief Returns whether one compressed entry can accept one more value.
     * @param entry Compressed history entry.
     * @return True when entry may be extended.
     */
    [[nodiscard]] bool hasSpaceLeft(const CompressedHistoryEntry& entry) const;

    /**
     * @brief Returns whether one interval matches configured bounds.
     * @param newIntervalMs Interval to validate.
     * @param referenceIntervalMs Interval reference derived from already joined values.
     * @return True when new interval is within configured bounds.
     */
    [[nodiscard]] bool isMatchingInterval(std::int64_t newIntervalMs,
                                          std::int64_t referenceIntervalMs) const;

    /**
     * @brief Tries to convert one time entry to interval representation.
     * @param entry Time entry candidate.
     * @return Converted interval when matching, otherwise nullopt.
     */
    [[nodiscard]] std::optional<IntervalHistoryEntry>
    tryConvertTimeToInterval(const TimeHistoryEntry& entry) const;

    /**
     * @brief Returns newest contiguous identical-value timestamps from one timeValue entry.
     * @param entry TimeValue entry.
     * @return Ordered oldest-to-newest timestamp list.
     */
    [[nodiscard]] static std::vector<std::int64_t>
    newestIdenticalValueTimestamps(const TimeValueHistoryEntry& entry);

    /**
     * @brief Returns interval candidate of newest timestamps from one time entry.
     * @param entry Time entry.
     * @return Interval candidate with amount and time bounds.
     */
    [[nodiscard]] IntervalHistoryEntry
    newestIntervalCandidate(const TimeHistoryEntry& entry) const;

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

    /**
     * @brief Appends one single compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Single compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendSingleHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                         const SingleHistoryEntry& entry,
                                         bool includeReason);

    /**
     * @brief Appends one timeValue compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry TimeValue compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendTimeValueHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                              const TimeValueHistoryEntry& entry,
                                              bool includeReason);

    /**
     * @brief Appends one time compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Time compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendTimeHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                         const TimeHistoryEntry& entry,
                                         bool includeReason);

    /**
     * @brief Appends one interval compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Interval compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendIntervalHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                           const IntervalHistoryEntry& entry,
                                           bool includeReason);

    /**
     * @brief Re-compresses decompressed history for persistence restore.
     * @param history Decompressed history list in newest-first order.
     * @return Compressed representation.
     */
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
    * @brief Compares current node with snapshot node for diff mode.
     * @param current Current node.
     * @param snapshot Snapshot node.
     * @return True when equivalent for value and optional reason/timestamp checks.
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
