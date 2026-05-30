#pragma once

/**
 * @file message_tree_node.h
 * @brief MessageTree node/result DTO types used by public query APIs.
 */

#include "yaha/message/message.h"

#include <cstddef>
#include <vector>

namespace yaha {

/**
 * @brief One historic value entry for a topic node.
 */
class MessageTreeHistoryEntry {
public:
    std::int64_t timeMs{0};
    Value value{std::string{}};

    /**
     * @brief Sets history reason list from API-level reason entries.
     * @param reason Reason list.
     */
    void setReasonList(const ReasonList& reason);

    /**
     * @brief Returns API-level reason list.
     * @return Decompressed reason list.
     */
    [[nodiscard]] ReasonList reason() const;

    /**
     * @brief Clears all reason entries.
     */
    void clearReason();

private:
    ReasonList reason_{};
};

/**
 * @brief Public node representation returned by tree queries.
 */
class MessageTreeNode {
public:
    std::string topic;
    std::int64_t timeMs{0};
    Value value{std::string{}};

    MessageTreeNode();

    /**
     * @brief Sets node reason list from API-level reason entries.
     * @param reason Reason list.
     */
    void setReasonList(const ReasonList& reason);

    /**
     * @brief Returns API-level reason list.
     * @return Decompressed reason list.
     */
    [[nodiscard]] ReasonList reason() const;

    /**
     * @brief Clears node reason list.
     */
    void clearReason();

    /**
     * @brief Returns immutable history entries.
     * @return History entry list.
     */
    [[nodiscard]] const std::vector<MessageTreeHistoryEntry>& history() const;

    /**
     * @brief Replaces full history entry list.
     * @param historyEntries History entries.
     */
    void setHistoryEntries(std::vector<MessageTreeHistoryEntry> historyEntries);

    /**
     * @brief Adds one history entry.
     * @param historyEntry History entry.
     */
    void addHistoryEntry(const MessageTreeHistoryEntry& historyEntry);

    /**
     * @brief Adds one history entry from API-level values.
     * @param timeMilliseconds History timestamp.
     * @param historyValue History value.
     * @param historyReason History reason list.
     */
    void addHistoryEntry(std::int64_t timeMilliseconds,
                         Value historyValue,
                         const ReasonList& historyReason);

    /**
     * @brief Removes one history entry by index.
     * @param historyIndex Entry index.
     * @return True when one entry was removed.
     */
    [[nodiscard]] bool removeHistoryEntry(std::size_t historyIndex);

    /**
     * @brief Clears all history entries.
     */
    void clearHistory();

private:
    ReasonList reason_{};
    std::vector<MessageTreeHistoryEntry> history_{};
};

} // namespace yaha