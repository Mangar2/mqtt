#include "yaha/message_store/message_tree.h"

#include "yaha/message_store/iso_timestamp_parser.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::uint32_t k_minimum_trimmed_history_length{1U};

template <typename... Visitor>
struct VariantVisitor : Visitor... {
    using Visitor::operator()...;
};

template <typename... Visitor>
VariantVisitor(Visitor...) -> VariantVisitor<Visitor...>;

} // namespace

void MessageTree::appendHistory(NodeData& data) const {
    MessageTreeHistoryEntry entry{};
    entry.timeMs = data.timeMs;
    entry.value = data.value;
    entry.reason = data.reason;
    addHistoryEntry(data.compressedHistory, entry);
}

void MessageTree::addHistoryEntry(std::vector<CompressedHistoryEntry>& history,
                                  const MessageTreeHistoryEntry& entryToAdd) const {
    const auto insertSingleAsNewest = [&history, &entryToAdd]() {
        history.insert(history.begin(), CompressedHistoryEntry{SingleHistoryEntry{entryToAdd}});
    };

    if (history.empty()) {
        insertSingleAsNewest();
        return;
    }

    CompressedHistoryEntry& newest = history.front();
    if (!areReasonMessagesEqual(entryToAdd.reason, reasonOf(newest)) || !hasSpaceLeft(newest)) {
        insertSingleAsNewest();
        return;
    }

    if (auto* singleEntry = std::get_if<SingleHistoryEntry>(&newest.data)) {
        TimeValueHistoryEntry timeValueEntry{};
        timeValueEntry.values.push_back({singleEntry->entry.timeMs, singleEntry->entry.value});
        timeValueEntry.values.push_back({entryToAdd.timeMs, entryToAdd.value});
        timeValueEntry.reason = singleEntry->entry.reason;
        newest.data = std::move(timeValueEntry);
        return;
    }

    if (std::holds_alternative<TimeValueHistoryEntry>(newest.data)) {
        addOrConvertTimeValueEntry(newest, history, entryToAdd);
        return;
    }

    if (std::holds_alternative<TimeHistoryEntry>(newest.data)) {
        addOrConvertTimeEntry(newest, history, entryToAdd);
        return;
    }

    addOrConvertIntervalEntry(newest, history, entryToAdd);
}

void MessageTree::addOrConvertTimeValueEntry(CompressedHistoryEntry& newest,
                                             std::vector<CompressedHistoryEntry>& history,
                                             const MessageTreeHistoryEntry& entryToAdd) const {
    auto* timeValueEntry = std::get_if<TimeValueHistoryEntry>(&newest.data);
    if (timeValueEntry == nullptr) {
        return;
    }

    timeValueEntry->values.push_back({entryToAdd.timeMs, entryToAdd.value});

    if (timeValueEntry->values.size() == config_.lengthForFurtherCompression) {
        const std::vector<std::int64_t> timestamps = newestIdenticalValueTimestamps(*timeValueEntry);
        if (timestamps.size() == timeValueEntry->values.size()) {
            TimeHistoryEntry timeEntry{};
            timeEntry.value = timeValueEntry->values.back().second;
            timeEntry.timestamps = timestamps;
            timeEntry.reason = timeValueEntry->reason;

            if (auto intervalEntry = tryConvertTimeToInterval(timeEntry); intervalEntry.has_value()) {
                newest.data = std::move(*intervalEntry);
            } else {
                newest.data = std::move(timeEntry);
            }
        }
    }

    auto* currentTimeValueEntry = std::get_if<TimeValueHistoryEntry>(&newest.data);
    if (currentTimeValueEntry == nullptr) {
        return;
    }

    const std::vector<std::int64_t> timestamps = newestIdenticalValueTimestamps(*currentTimeValueEntry);
    if (timestamps.size() < config_.lengthForFurtherCompression) {
        return;
    }

    const Value newestValue = currentTimeValueEntry->values.back().second;
    currentTimeValueEntry->values.resize(currentTimeValueEntry->values.size() - timestamps.size());

    TimeHistoryEntry newTimeEntry{};
    newTimeEntry.value = newestValue;
    newTimeEntry.timestamps = timestamps;
    newTimeEntry.reason = currentTimeValueEntry->reason;

    if (auto intervalEntry = tryConvertTimeToInterval(newTimeEntry); intervalEntry.has_value()) {
        history.insert(history.begin(), CompressedHistoryEntry{std::move(*intervalEntry)});
        return;
    }

    history.insert(history.begin(), CompressedHistoryEntry{std::move(newTimeEntry)});
}

void MessageTree::addOrConvertTimeEntry(CompressedHistoryEntry& newest,
                                        std::vector<CompressedHistoryEntry>& history,
                                        const MessageTreeHistoryEntry& entryToAdd) const {
    auto* timeEntry = std::get_if<TimeHistoryEntry>(&newest.data);
    if (timeEntry == nullptr) {
        return;
    }

    if (timeEntry->value != entryToAdd.value) {
        history.insert(history.begin(), CompressedHistoryEntry{SingleHistoryEntry{entryToAdd}});
        return;
    }

    timeEntry->timestamps.push_back(entryToAdd.timeMs);

    const IntervalHistoryEntry intervalEntry = newestIntervalCandidate(*timeEntry);
    if (intervalEntry.amount < config_.lengthForFurtherCompression) {
        return;
    }

    timeEntry->timestamps.resize(timeEntry->timestamps.size() - intervalEntry.amount);
    history.insert(history.begin(), CompressedHistoryEntry{intervalEntry});
}

void MessageTree::addOrConvertIntervalEntry(CompressedHistoryEntry& newest,
                                            std::vector<CompressedHistoryEntry>& history,
                                            const MessageTreeHistoryEntry& entryToAdd) const {
    auto* intervalEntry = std::get_if<IntervalHistoryEntry>(&newest.data);
    if (intervalEntry == nullptr || intervalEntry->amount <= 1U) {
        history.insert(history.begin(), CompressedHistoryEntry{SingleHistoryEntry{entryToAdd}});
        return;
    }

    const std::int64_t referenceIntervalMs =
        (intervalEntry->lastTimeMs - intervalEntry->firstTimeMs) /
        static_cast<std::int64_t>(intervalEntry->amount - 1U);
    const std::int64_t newIntervalMs = entryToAdd.timeMs - intervalEntry->lastTimeMs;

    if (intervalEntry->value != entryToAdd.value ||
        !isMatchingInterval(newIntervalMs, referenceIntervalMs)) {
        history.insert(history.begin(), CompressedHistoryEntry{SingleHistoryEntry{entryToAdd}});
        return;
    }

    intervalEntry->lastTimeMs = entryToAdd.timeMs;
    intervalEntry->amount += 1U;
}

bool MessageTree::areReasonMessagesEqual(const std::vector<ReasonEntry>& left,
                                         const std::vector<ReasonEntry>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t idx = 0U; idx < left.size(); ++idx) {
        if (left[idx].message != right[idx].message) {
            return false;
        }
    }

    return true;
}

const std::vector<ReasonEntry>& MessageTree::reasonOf(const CompressedHistoryEntry& entry) {
    if (const auto* singleEntry = std::get_if<SingleHistoryEntry>(&entry.data)) {
        return singleEntry->entry.reason;
    }
    if (const auto* timeValueEntry = std::get_if<TimeValueHistoryEntry>(&entry.data)) {
        return timeValueEntry->reason;
    }
    if (const auto* timeEntry = std::get_if<TimeHistoryEntry>(&entry.data)) {
        return timeEntry->reason;
    }
    return std::get<IntervalHistoryEntry>(entry.data).reason;
}

bool MessageTree::hasSpaceLeft(const CompressedHistoryEntry& entry) const {
    if (std::holds_alternative<SingleHistoryEntry>(entry.data)) {
        return config_.maxValuesPerHistoryEntry > 1U;
    }

    if (const auto* timeValueEntry = std::get_if<TimeValueHistoryEntry>(&entry.data)) {
        return timeValueEntry->values.size() < config_.maxValuesPerHistoryEntry;
    }

    if (const auto* timeEntry = std::get_if<TimeHistoryEntry>(&entry.data)) {
        return timeEntry->timestamps.size() < config_.maxValuesPerHistoryEntry;
    }

    return true;
}

bool MessageTree::isMatchingInterval(std::int64_t newIntervalMs,
                                     std::int64_t referenceIntervalMs) const {
    const double newInterval = static_cast<double>(newIntervalMs);
    const double referenceInterval = static_cast<double>(referenceIntervalMs);
    const double upperBound =
        (referenceInterval * config_.upperBoundFactor) +
        static_cast<double>(config_.upperBoundAddInMilliseconds);
    const double lowerBound =
        (referenceInterval * config_.lowerBoundFactor) -
        static_cast<double>(config_.lowerBoundSubInMilliseconds);
    return newInterval < upperBound && newInterval > lowerBound;
}

std::optional<MessageTree::IntervalHistoryEntry>
MessageTree::tryConvertTimeToInterval(const TimeHistoryEntry& entry) const {
    if (entry.timestamps.size() < 2U) {
        return std::nullopt;
    }

    const std::int64_t firstTimeMs = entry.timestamps.front();
    const std::int64_t lastTimeMs = entry.timestamps.back();
    const std::int64_t referenceIntervalMs =
        (lastTimeMs - firstTimeMs) / static_cast<std::int64_t>(entry.timestamps.size() - 1U);

    for (std::size_t idx = 1U; idx < entry.timestamps.size(); ++idx) {
        const std::int64_t newIntervalMs = entry.timestamps[idx] - entry.timestamps[idx - 1U];
        if (!isMatchingInterval(newIntervalMs, referenceIntervalMs)) {
            return std::nullopt;
        }
    }

    return IntervalHistoryEntry{
        .amount = static_cast<std::uint32_t>(entry.timestamps.size()),
        .value = entry.value,
        .reason = entry.reason,
        .firstTimeMs = firstTimeMs,
        .lastTimeMs = lastTimeMs
    };
}

std::vector<std::int64_t>
MessageTree::newestIdenticalValueTimestamps(const TimeValueHistoryEntry& entry) {
    std::vector<std::int64_t> timestamps{};
    if (entry.values.empty()) {
        return timestamps;
    }

    const Value newestValue = entry.values.back().second;
    for (std::size_t idx = entry.values.size(); idx > 0U; --idx) {
        const auto& timeValue = entry.values[idx - 1U];
        if (timeValue.second != newestValue) {
            break;
        }
        timestamps.push_back(timeValue.first);
    }

    std::reverse(timestamps.begin(), timestamps.end());
    return timestamps;
}

MessageTree::IntervalHistoryEntry
MessageTree::newestIntervalCandidate(const TimeHistoryEntry& entry) const {
    IntervalHistoryEntry result{
        .amount = 0U,
        .value = entry.value,
        .reason = entry.reason,
        .firstTimeMs = 0,
        .lastTimeMs = entry.timestamps.back()
    };

    if (entry.timestamps.size() <= config_.lengthForFurtherCompression) {
        return result;
    }

    const std::size_t entryAmount = entry.timestamps.size();
    const std::size_t minAmount = config_.lengthForFurtherCompression;
    const std::size_t minStartIndex = entryAmount - minAmount;
    result.firstTimeMs = entry.timestamps[minStartIndex];

    const std::int64_t expectedIntervalMs =
        (result.lastTimeMs - result.firstTimeMs) / static_cast<std::int64_t>(minAmount - 1U);

    std::size_t firstIncludedIndex = entryAmount - 1U;
    std::uint32_t includedAmount = 1U;

    for (std::size_t idx = entryAmount - 1U; idx > 0U; --idx) {
        const std::int64_t newIntervalMs = entry.timestamps[idx] - entry.timestamps[idx - 1U];
        if (!isMatchingInterval(newIntervalMs, expectedIntervalMs)) {
            break;
        }

        firstIncludedIndex = idx - 1U;
        includedAmount += 1U;
    }

    result.firstTimeMs = entry.timestamps[firstIncludedIndex];
    result.amount = includedAmount;

    return result;
}

void MessageTree::trimHistory(NodeData& data) const {
    if (data.compressedHistory.size() < config_.maxHistoryLength) {
        return;
    }

    std::uint32_t newLength = config_.maxHistoryLength - config_.historyHysterese;
    newLength = std::max(newLength, k_minimum_trimmed_history_length);

    if (data.compressedHistory.size() > newLength) {
        data.compressedHistory.resize(newLength);
    }
}

std::vector<MessageTreeHistoryEntry>
MessageTree::decompressHistory(const std::vector<CompressedHistoryEntry>& compressed,
                               bool includeReason) {
    std::vector<MessageTreeHistoryEntry> history{};

    for (const auto& item : compressed) {
        std::visit(VariantVisitor{
                       [&](const SingleHistoryEntry& entry) {
                           appendSingleHistoryEntry(history, entry, includeReason);
                       },
                       [&](const TimeValueHistoryEntry& entry) {
                           appendTimeValueHistoryEntries(history, entry, includeReason);
                       },
                       [&](const TimeHistoryEntry& entry) {
                           appendTimeHistoryEntries(history, entry, includeReason);
                       },
                       [&](const IntervalHistoryEntry& entry) {
                           appendIntervalHistoryEntry(history, entry, includeReason);
                       }},
                   item.data);
    }

    return history;
}

void MessageTree::appendSingleHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                           const SingleHistoryEntry& entry,
                                           bool includeReason) {
    MessageTreeHistoryEntry decompressedEntry = entry.entry;
    if (!includeReason) {
        decompressedEntry.reason.clear();
    }
    history.push_back(std::move(decompressedEntry));
}

void MessageTree::appendTimeValueHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                                const TimeValueHistoryEntry& entry,
                                                bool includeReason) {
    std::vector<MessageTreeHistoryEntry> expanded{};
    expanded.reserve(entry.values.size());

    for (std::size_t idx = entry.values.size(); idx > 0U; --idx) {
        const auto& timeValue = entry.values[idx - 1U];
        expanded.push_back(MessageTreeHistoryEntry{
            .timeMs = timeValue.first,
            .value = timeValue.second,
            .reason = {}
        });
    }

    if (includeReason && !expanded.empty()) {
        expanded.back().reason = entry.reason;
    }

    history.insert(history.end(), expanded.begin(), expanded.end());
}

void MessageTree::appendTimeHistoryEntries(std::vector<MessageTreeHistoryEntry>& history,
                                           const TimeHistoryEntry& entry,
                                           bool includeReason) {
    std::vector<MessageTreeHistoryEntry> expanded{};
    expanded.reserve(entry.timestamps.size());

    for (std::size_t idx = entry.timestamps.size(); idx > 0U; --idx) {
        expanded.push_back(MessageTreeHistoryEntry{
            .timeMs = entry.timestamps[idx - 1U],
            .value = entry.value,
            .reason = {}
        });
    }

    if (includeReason && !expanded.empty()) {
        expanded.back().reason = entry.reason;
    }

    history.insert(history.end(), expanded.begin(), expanded.end());
}

void MessageTree::appendIntervalHistoryEntry(std::vector<MessageTreeHistoryEntry>& history,
                                             const IntervalHistoryEntry& entry,
                                             bool includeReason) {
    MessageTreeHistoryEntry decompressedEntry{};
    decompressedEntry.timeMs = entry.firstTimeMs;
    decompressedEntry.value = entry.value;
    if (includeReason) {
        decompressedEntry.reason.push_back(ReasonEntry{
            .message = std::format("regular update, amount: {}", entry.amount),
            .timestamp = toIsoTimestampMilliseconds(entry.firstTimeMs)
        });
        decompressedEntry.reason.insert(decompressedEntry.reason.end(),
                                        entry.reason.begin(),
                                        entry.reason.end());
    }

    history.push_back(std::move(decompressedEntry));
}

std::vector<MessageTree::CompressedHistoryEntry>
MessageTree::compressHistory(const std::vector<MessageTreeHistoryEntry>& history) const {
    std::vector<CompressedHistoryEntry> compressed{};
    for (auto iter = history.rbegin(); iter != history.rend(); ++iter) {
        addHistoryEntry(compressed, *iter);
    }
    return compressed;
}

} // namespace yaha
