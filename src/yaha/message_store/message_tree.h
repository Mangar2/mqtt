#pragma once

/**
 * @file message_tree.h
 * @brief MessageTree data structure for MessageStore state and history.
 */

#include "yaha/message/message.h"
#include "yaha/message_store/message_tree_node.h"
#include "yaha/message_store/tree_node.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

/**
 * @brief Snapshot node used by diff query mode.
 */
struct MessageSnapshot {
    std::string topic;                          ///< Full topic path.
    Value value{std::string{}};             ///< Snapshot value.
    ReasonList reason;                          ///< Snapshot reason chain.
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
     * @brief Compression status counters of internal history representation.
     */
    struct CompressionStats {
        std::uint64_t currentNodeCount{0U};            ///< Topic nodes that currently hold a value.
        std::uint64_t totalStoredMessageCount{0U};     ///< Current nodes + represented history messages.
        std::uint64_t historyBucketCount{0U};          ///< Number of compressed history buckets.
        std::uint64_t totalReasonEntryCount{0U};       ///< Total number of stored reason entries.
        std::uint64_t totalDirectoryStringCount{0U};   ///< Total number of unique reason strings across per-node directories.
        double reasonEntriesPerDirectoryString{0.0};   ///< Ratio totalReasonEntryCount / totalDirectoryStringCount.
        std::uint64_t singleBucketCount{0U};           ///< Bucket count of SingleHistoryEntry.
        std::uint64_t timeValueBucketCount{0U};        ///< Bucket count of TimeValueHistoryEntry.
        std::uint64_t timeBucketCount{0U};             ///< Bucket count of TimeHistoryEntry.
        std::uint64_t intervalBucketCount{0U};         ///< Bucket count of IntervalHistoryEntry.
        std::uint64_t representedSingleCount{0U};      ///< Logical history messages represented by Single buckets.
        std::uint64_t representedTimeValueCount{0U};   ///< Logical history messages represented by TimeValue buckets.
        std::uint64_t representedTimeValueStringCount{0U}; ///< String values represented by TimeValue buckets.
        std::uint64_t representedTimeValueDoubleCount{0U}; ///< Numeric values represented by TimeValue buckets.
        std::uint64_t representedTimeCount{0U};        ///< Logical history messages represented by Time buckets.
        std::uint64_t representedIntervalCount{0U};    ///< Logical history messages represented by Interval buckets.
    };

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
    getNodes(const std::vector<MessageSnapshot>& snapshot,
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

    /**
     * @brief Returns counters about internal compression state.
     * @return Compression counters for current tree content.
     */
    [[nodiscard]] CompressionStats compressionStats() const;

    /**
     * @brief Writes full internal tree in compressed form to stream.
     * @param stream Destination stream.
     * @return True on successful write.
     */
    [[nodiscard]] bool writeCompressed(std::ostream& stream) const;

    /**
     * @brief Reads full internal tree in compressed form from stream.
     * @param stream Source stream.
     * @return True on successful parse and replace.
     */
    [[nodiscard]] bool readCompressed(std::istream& stream);

private:
    using CompactReasonList = MessageTreeCompactReasonList;
    using SingleHistoryEntry = MessageTreeSingleHistoryEntry;
    using TimeValueHistoryEntry = MessageTreeTimeValueHistoryEntry;
    using TimeHistoryEntry = MessageTreeTimeHistoryEntry;
    using IntervalHistoryEntry = MessageTreeIntervalHistoryEntry;
    using CompressedHistoryEntry = MessageTreeCompressedHistoryEntry;

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
     * @brief Finds one child index by exact segment name.
     * @param node Parent node.
     * @param segment Child segment key.
     * @return Child index or nullopt when absent.
     */
    [[nodiscard]] static std::optional<std::size_t>
    findChildIndex(const TreeNode& node, const std::string& segment);

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
                         const MessageTreeHistoryEntry& entryToAdd,
                         StringDirectory& reasonDirectory) const;

    /**
     * @brief Returns whether two reason chains are compression-compatible.
     * @param left Left reason chain.
     * @param right Right reason chain.
     * @return True when both chains have equal message texts.
     */
    [[nodiscard]] static bool areReasonMessagesEqual(const CompactReasonList& left,
                                                     const CompactReasonList& right);

    /**
     * @brief Returns reason chain associated with one compressed history entry.
     * @param entry Compressed history entry.
     * @return Associated reason chain.
     */
    [[nodiscard]] static CompactReasonList reasonOf(const CompressedHistoryEntry& entry);

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
                               const MessageTreeHistoryEntry& entryToAdd,
                               StringDirectory& reasonDirectory) const;

    /**
     * @brief Extends or replaces a newest interval compressed entry with a new value.
     * @param newest Mutable newest compressed entry.
     * @param history Mutable full compressed history list.
     * @param entryToAdd New history entry to integrate.
     */
    void addOrConvertIntervalEntry(CompressedHistoryEntry& newest,
                                   std::vector<CompressedHistoryEntry>& history,
                                   const MessageTreeHistoryEntry& entryToAdd,
                                   StringDirectory& reasonDirectory) const;

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
                      const StringDirectory& reasonDirectory,
                      bool includeReason);

    /**
     * @brief Appends one single compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Single compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendSingleHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                         const SingleHistoryEntry& entry,
                                         const StringDirectory& reasonDirectory,
                                         bool includeReason);

    /**
     * @brief Appends one timeValue compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry TimeValue compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendTimeValueHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                              const TimeValueHistoryEntry& entry,
                                              const StringDirectory& reasonDirectory,
                                              bool includeReason);

    /**
     * @brief Appends one time compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Time compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendTimeHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                         const TimeHistoryEntry& entry,
                                         const StringDirectory& reasonDirectory,
                                         bool includeReason);

    /**
     * @brief Appends one interval compressed history entry to decompressed output.
     * @param history Mutable decompressed output sink.
     * @param entry Interval compressed entry.
     * @param includeReason Include reason output.
     */
    static void appendIntervalHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                           const IntervalHistoryEntry& entry,
                                           const StringDirectory& reasonDirectory,
                                           bool includeReason);

    /**
     * @brief Re-compresses decompressed history for persistence restore.
     * @param history Decompressed history list in newest-first order.
     * @return Compressed representation.
     */
    [[nodiscard]] std::vector<CompressedHistoryEntry>
    compressHistory(const std::vector<MessageTreeHistoryEntry>& history,
                    StringDirectory& reasonDirectory) const;

    static void compactReasonDirectory(NodeData& data);

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
                                             const MessageSnapshot& snapshot);

    /**
     * @brief Builds a detached ReasonList copy with reserved target capacity.
     * @param source Source reason list.
     * @return Detached copy suitable for move-assignment at target site.
     */
    [[nodiscard]] static CompactReasonList toCompactReasonList(const ReasonList& source,
                                                               StringDirectory& reasonDirectory);
    [[nodiscard]] static ReasonList toReasonList(const CompactReasonList& source,
                                                 const StringDirectory& reasonDirectory);

    [[nodiscard]] static bool writeValueToken(std::ostream& stream, const Value& value);
    [[nodiscard]] static bool readValueToken(std::istream& stream, Value& value);
    [[nodiscard]] static bool writeReasonListToken(std::ostream& stream,
                                                   const CompactReasonList& reasonList,
                                                   const StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readReasonListToken(std::istream& stream,
                                                  CompactReasonList& reasonList,
                                                  StringDirectory& reasonDirectory);
    [[nodiscard]] static bool writeCompressedHistoryEntry(std::ostream& stream,
                                                          const CompressedHistoryEntry& entry,
                                                          const StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readCompressedHistoryEntry(std::istream& stream,
                                                         CompressedHistoryEntry& entry,
                                                         StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readSingleHistoryEntry(std::istream& stream,
                                                     CompressedHistoryEntry& entry,
                                                     StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readTimeValueHistoryEntry(std::istream& stream,
                                                        CompressedHistoryEntry& entry,
                                                        StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readTimeHistoryEntry(std::istream& stream,
                                                   CompressedHistoryEntry& entry,
                                                   StringDirectory& reasonDirectory);
    [[nodiscard]] static bool readIntervalHistoryEntry(std::istream& stream,
                                                       CompressedHistoryEntry& entry,
                                                       StringDirectory& reasonDirectory);
    [[nodiscard]] bool writeCompressedTreeNode(std::ostream& stream, const TreeNode& node) const;
    [[nodiscard]] bool readCompressedTreeNode(std::istream& stream, TreeNode& node);

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
