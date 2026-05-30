#include "yaha/message_store/message_tree_node.h"

#include <cstddef>
#include <utility>

namespace yaha {

void MessageTreeHistoryEntry::setReasonList(const ReasonList& reason) {
    reason_ = reason;
}

ReasonList MessageTreeHistoryEntry::reason() const {
    return reason_;
}

void MessageTreeHistoryEntry::clearReason() {
    reason_.clear();
}

MessageTreeNode::MessageTreeNode() = default;

void MessageTreeNode::setReasonList(const ReasonList& reason) {
    reason_ = reason;
}

ReasonList MessageTreeNode::reason() const {
    return reason_;
}

void MessageTreeNode::clearReason() {
    reason_.clear();
}

const std::vector<MessageTreeHistoryEntry>& MessageTreeNode::history() const {
    return history_;
}

void MessageTreeNode::setHistoryEntries(std::vector<MessageTreeHistoryEntry> historyEntries) {
    history_.clear();
    history_.reserve(historyEntries.size());
    for (auto& historyEntry : historyEntries) {
        history_.push_back(std::move(historyEntry));
    }
}

void MessageTreeNode::addHistoryEntry(const MessageTreeHistoryEntry& historyEntry) {
    history_.push_back(historyEntry);
}

void MessageTreeNode::addHistoryEntry(std::int64_t timeMilliseconds,
                                      Value historyValue,
                                      const ReasonList& historyReason) {
    MessageTreeHistoryEntry historyEntry{};
    historyEntry.timeMs = timeMilliseconds;
    historyEntry.value = std::move(historyValue);
    historyEntry.setReasonList(historyReason);
    history_.push_back(std::move(historyEntry));
}

bool MessageTreeNode::removeHistoryEntry(std::size_t historyIndex) {
    if (historyIndex >= history_.size()) {
        return false;
    }

    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyIndex));
    return true;
}

void MessageTreeNode::clearHistory() {
    history_.clear();
}

} // namespace yaha