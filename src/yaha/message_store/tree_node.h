#pragma once

/**
 * @file tree_node.h
 * @brief Internal MessageTree node and compressed-history storage types.
 */

#include "yaha/message/message.h"
#include "yaha/message_store/string_directory.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace yaha {

/**
 * @brief Compact internal reason entry using a StringDirectory slot index.
 */
struct TreeNodeReasonEntry {
    slotIndex_t messageSlotIndex{0U};
    std::int64_t timestampMs{0};
    std::uint8_t fractionalDigits{0U};
};

using MessageTreeCompactReasonList = std::vector<TreeNodeReasonEntry>;

/**
 * @brief Compressed entry with exactly one historic value.
 */
struct MessageTreeSingleHistoryEntry {
    std::int64_t timeMs{0}; ///< Stored history timestamp.
    Value value{std::string{}}; ///< Stored history value.
    MessageTreeCompactReasonList reason; ///< Stored history reason chain.
};

/**
 * @brief Compressed entry with multiple values sharing one reason chain.
 */
struct MessageTreeTimeValueHistoryEntry {
    std::vector<std::pair<std::int64_t, Value>> values; ///< Ordered oldest-to-newest time/value pairs.
    MessageTreeCompactReasonList reason; ///< Reason chain of the oldest element.
};

/**
 * @brief Compressed entry with multiple timestamps sharing one value and reason chain.
 */
struct MessageTreeTimeHistoryEntry {
    Value value{std::string{}}; ///< Shared value of all timestamps.
    std::vector<std::int64_t> timestamps; ///< Ordered oldest-to-newest timestamps.
    MessageTreeCompactReasonList reason; ///< Reason chain of the oldest element.
};

/**
 * @brief Compressed entry representing regular updates as one interval block.
 */
struct MessageTreeIntervalHistoryEntry {
    std::uint32_t amount{0U}; ///< Amount of compressed entries in this block.
    Value value{std::string{}}; ///< Shared value of the interval block.
    MessageTreeCompactReasonList reason; ///< Reason chain of the oldest element.
    std::int64_t firstTimeMs{0}; ///< Oldest timestamp in the block.
    std::int64_t lastTimeMs{0}; ///< Newest timestamp in the block.
};

/**
 * @brief Compressed internal history bucket.
 */
struct MessageTreeCompressedHistoryEntry {
    std::variant<MessageTreeSingleHistoryEntry,
                 MessageTreeTimeValueHistoryEntry,
                 MessageTreeTimeHistoryEntry,
                 MessageTreeIntervalHistoryEntry> data{}; ///< Type-specific compressed representation.
};

/**
 * @brief Internal payload state of one data node.
 */
struct NodeData {
    std::int64_t timeMs{0}; ///< Current timestamp.
    Value value{std::string{}}; ///< Current value.
    StringDirectory reasonDirectory{}; ///< Per-node reason-message directory.
    MessageTreeCompactReasonList reason; ///< Current reason.
    std::vector<MessageTreeCompressedHistoryEntry> compressedHistory; ///< Compressed historic values.
};

/**
 * @brief Internal topic-segment node.
 */
struct TreeNode {
    std::string topicPath; ///< Full topic path for this node.
    std::vector<std::pair<std::string, TreeNode>> children; ///< Child segments.
    std::unique_ptr<NodeData> data{}; ///< Current data payload; nullptr means no data.
};

} // namespace yaha
