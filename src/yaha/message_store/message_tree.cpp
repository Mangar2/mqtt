#include "yaha/message_store/message_tree.h"

#include "yaha/message_store/compact_reason_entry.h"
#include "yaha/message_store/iso_timestamp_parser.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::int64_t k_millis_per_day{86400000};
constexpr std::uint32_t k_legacy_length_for_further_compression_minimum{3U};

[[nodiscard]] bool reasonListsEqual(const ReasonList& left,
                                    const ReasonList& right) {
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

[[nodiscard]] std::string formatReasonTimestamp(const TreeNodeReasonEntry& compactReason) {
    std::string timestampText = toIsoTimestampMilliseconds(compactReason.timestampMs);
    if (compactReason.fractionalDigits > 0U && timestampText.find('.') == std::string::npos) {
        timestampText.insert(timestampText.size() - 1U, ".000");
    }

    const std::size_t dotPos = timestampText.find('.');
    const std::size_t zonePos = dotPos == std::string::npos
        ? std::string::npos
        : timestampText.find('Z', dotPos + 1U);

    if (compactReason.fractionalDigits == 0U && dotPos != std::string::npos && zonePos != std::string::npos) {
        timestampText.erase(dotPos, zonePos - dotPos);
        return timestampText;
    }

    if (compactReason.fractionalDigits < 3U && dotPos != std::string::npos && zonePos != std::string::npos) {
        timestampText.erase(dotPos + 1U + compactReason.fractionalDigits,
                            zonePos - (dotPos + 1U + compactReason.fractionalDigits));
    }

    return timestampText;
}

[[nodiscard]] bool readReasonTimestampAndFraction(std::istream& stream,
                                                  std::int64_t& timestampMs,
                                                  std::uint8_t& fractionalDigits) {
    stream >> std::ws;
    const int nextChar = stream.peek();
    if (nextChar == std::char_traits<char>::eof()) {
        return false;
    }

    if (nextChar == '"') {
        std::string timestampText{};
        if (!(stream >> std::quoted(timestampText))) {
            return false;
        }

        const CompactReasonEntry compactReason = CompactReasonEntry::fromStrings("", timestampText);
        timestampMs = compactReason.timestampMs();
        fractionalDigits = compactReason.fractionalDigits();
        return true;
    }

    std::int64_t parsedTimestampMs = 0;
    std::uint32_t parsedFractionalDigits = 0U;
    if (!(stream >> parsedTimestampMs >> parsedFractionalDigits)) {
        return false;
    }

    if (parsedFractionalDigits > static_cast<std::uint32_t>(std::numeric_limits<std::uint8_t>::max())) {
        return false;
    }

    timestampMs = parsedTimestampMs;
    fractionalDigits = static_cast<std::uint8_t>(parsedFractionalDigits);
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

void MessageTree::addData(const Message& message) {
    Message::validate(message);

    TreeNode* node = ensurePath(message.topic());
    if (node == nullptr) {
        throw std::runtime_error{"MessageTree failed to create topic path"};
    }

    if (node->data != nullptr) {
        appendHistory(*node->data);
        trimHistory(*node->data);
    }

    std::int64_t effectiveTimeMs = nowMilliseconds();
    const auto& reasons = message.reason();
    if (!reasons.empty()) {
        std::int64_t reasonTimeMs = 0;
        (void)tryParseIsoTimestampMilliseconds(reasons.front().timestamp, reasonTimeMs);
        if (reasonTimeMs > 0) {
            effectiveTimeMs = reasonTimeMs;

            if (node->data != nullptr && effectiveTimeMs <= node->data->timeMs) {
                std::int64_t newestReasonTimeMs = effectiveTimeMs;
                for (std::size_t idx = 1U; idx < reasons.size(); ++idx) {
                    std::int64_t candidateTimeMs = 0;
                    (void)tryParseIsoTimestampMilliseconds(reasons[idx].timestamp, candidateTimeMs);
                    if (candidateTimeMs <= 0) {
                        continue;
                    }
                    newestReasonTimeMs = std::max(newestReasonTimeMs, candidateTimeMs);
                }
                if (newestReasonTimeMs > node->data->timeMs) {
                    effectiveTimeMs = newestReasonTimeMs;
                }
            }
        }
    }
    if (node->data == nullptr) {
        node->data = std::make_unique<NodeData>();
    }

    node->data->timeMs = effectiveTimeMs;
    node->data->value = message.value();
    CompactReasonList detachedReasonForNode =
        toCompactReasonList(message.reason(), node->data->reasonDirectory);
    node->data->reason = std::move(detachedReasonForNode);
    compactReasonDirectory(*node->data);
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
MessageTree::getNodes(const std::vector<MessageSnapshot>& snapshot,
                      bool includeHistory,
                      bool includeReason) const {
    std::vector<MessageTreeNode> diff{};
    diff.reserve(snapshot.size());
    for (const auto& requiredNode : snapshot) {
        const TreeNode* current = findPath(requiredNode.topic);
        if (current == nullptr || current->data == nullptr) {
            continue;
        }

        MessageTreeNode currentNode{};
        currentNode.topic = current->topicPath;
        currentNode.timeMs = current->data->timeMs;
        currentNode.value = current->data->value;
        currentNode.setReasonList(toReasonList(current->data->reason, current->data->reasonDirectory));
        currentNode.setHistoryEntries(includeHistory
            ? decompressHistory(current->data->compressedHistory, current->data->reasonDirectory, true)
            : std::vector<MessageTreeHistoryEntry>{});

        if (!snapshotEquals(currentNode, requiredNode)) {
            if (!includeReason) {
                currentNode.clearReason();
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

        target->data = std::make_unique<NodeData>();
        target->data->timeMs = node.timeMs;
        target->data->value = node.value;
        CompactReasonList detachedReasonForTarget =
            toCompactReasonList(node.reason(), target->data->reasonDirectory);
        target->data->reason = std::move(detachedReasonForTarget);
        target->data->compressedHistory = compressHistory(node.history(), target->data->reasonDirectory);
        trimHistory(*target->data);
        compactReasonDirectory(*target->data);
    }
}

MessageTree::CompactReasonList MessageTree::toCompactReasonList(const ReasonList& source,
                                                                StringDirectory& reasonDirectory) {
    CompactReasonList detachedReason{};
    detachedReason.reserve(source.size());

    for (const auto& sourceEntry : source) {
        const CompactReasonEntry compactReason =
            CompactReasonEntry::fromStrings(sourceEntry.message, sourceEntry.timestamp);
        detachedReason.push_back(TreeNodeReasonEntry{
            .messageSlotIndex = reasonDirectory.add(sourceEntry.message),
            .timestampMs = compactReason.timestampMs(),
            .fractionalDigits = compactReason.fractionalDigits()
        });
    }

    return detachedReason;
}

ReasonList MessageTree::toReasonList(const CompactReasonList& source,
                                     const StringDirectory& reasonDirectory) {
    ReasonList detachedReason{};
    detachedReason.reserve(source.size());

    for (const auto& sourceEntry : source) {
        const auto messageText = reasonDirectory.get(sourceEntry.messageSlotIndex);
        if (!messageText.has_value()) {
            throw std::runtime_error{"MessageTree reason slot lookup failed"};
        }

        ReasonEntry targetEntry{};
        targetEntry.message = *messageText;
        if (sourceEntry.timestampMs != 0) {
            targetEntry.timestamp = formatReasonTimestamp(sourceEntry);
        }
        detachedReason.push_back(std::move(targetEntry));
    }

    return detachedReason;
}

std::size_t MessageTree::cleanup(std::uint32_t daysWithoutUpdate) {
    const std::int64_t cutoffMs = nowMilliseconds() -
        (static_cast<std::int64_t>(daysWithoutUpdate) * k_millis_per_day);
    return cleanupNode(root_, cutoffMs);
}

MessageTree::CompressionStats MessageTree::compressionStats() const {
    CompressionStats stats{};

    const auto accumulateReasonList = [&stats](const CompactReasonList& reasonList) {
        stats.totalReasonEntryCount += static_cast<std::uint64_t>(reasonList.size());
    };

    const auto accumulateHistoryEntry = [&stats, &accumulateReasonList](const CompressedHistoryEntry& historyEntry) {
        stats.historyBucketCount += 1U;
        std::visit(
            [&](const auto& typedEntry) {
                using EntryType = std::decay_t<decltype(typedEntry)>;

                if constexpr (std::is_same_v<EntryType, SingleHistoryEntry>) {
                    accumulateReasonList(typedEntry.reason);
                    stats.singleBucketCount += 1U;
                    stats.representedSingleCount += 1U;
                } else if constexpr (std::is_same_v<EntryType, TimeValueHistoryEntry>) {
                    accumulateReasonList(typedEntry.reason);
                    stats.timeValueBucketCount += 1U;
                    stats.representedTimeValueCount += static_cast<std::uint64_t>(typedEntry.values.size());
                    for (const auto& timeValue : typedEntry.values) {
                        if (std::holds_alternative<std::string>(timeValue.second)) {
                            stats.representedTimeValueStringCount += 1U;
                        } else {
                            stats.representedTimeValueDoubleCount += 1U;
                        }
                    }
                } else if constexpr (std::is_same_v<EntryType, TimeHistoryEntry>) {
                    accumulateReasonList(typedEntry.reason);
                    stats.timeBucketCount += 1U;
                    stats.representedTimeCount += static_cast<std::uint64_t>(typedEntry.timestamps.size());
                } else if constexpr (std::is_same_v<EntryType, IntervalHistoryEntry>) {
                    accumulateReasonList(typedEntry.reason);
                    stats.intervalBucketCount += 1U;
                    stats.representedIntervalCount += typedEntry.amount;
                }
            },
            historyEntry.data);
    };

    const auto accumulateNode = [&](const auto& self, const TreeNode& node) -> void {
        if (node.data != nullptr) {
            stats.currentNodeCount += 1U;
            accumulateReasonList(node.data->reason);

            for (const auto& historyEntry : node.data->compressedHistory) {
                accumulateHistoryEntry(historyEntry);
            }

            stats.totalDirectoryStringCount += static_cast<std::uint64_t>(node.data->reasonDirectory.size());
        }

        for (const auto& child : node.children) {
            self(self, child.second);
        }
    };
    accumulateNode(accumulateNode, root_);

    const std::uint64_t representedHistoryMessageCount =
        stats.representedSingleCount +
        stats.representedTimeValueCount +
        stats.representedTimeCount +
        stats.representedIntervalCount;
    stats.totalStoredMessageCount = stats.currentNodeCount + representedHistoryMessageCount;
    if (stats.totalDirectoryStringCount > 0U) {
        stats.reasonEntriesPerDirectoryString =
            static_cast<double>(stats.totalReasonEntryCount) /
            static_cast<double>(stats.totalDirectoryStringCount);
    }
    return stats;
}

bool MessageTree::writeCompressed(std::ostream& stream) const {
    return writeCompressedTreeNode(stream, root_);
}

bool MessageTree::readCompressed(std::istream& stream) {
    TreeNode parsedRoot{};
    if (!readCompressedTreeNode(stream, parsedRoot)) {
        return false;
    }

    root_ = std::move(parsedRoot);
    return true;
}

bool MessageTree::writeValueToken(std::ostream& stream, const Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        stream << "S " << std::quoted(std::get<std::string>(value)) << '\n';
        return static_cast<bool>(stream);
    }

    stream << "N " << std::get<double>(value) << '\n';
    return static_cast<bool>(stream);
}

bool MessageTree::readValueToken(std::istream& stream, Value& value) {
    std::string kind{};
    if (!(stream >> kind)) {
        return false;
    }

    if (kind == "S") {
        std::string text{};
        if (!(stream >> std::quoted(text))) {
            return false;
        }
        value = text;
        return true;
    }

    if (kind == "N") {
        double number = 0.0;
        if (!(stream >> number)) {
            return false;
        }
        value = number;
        return true;
    }

    return false;
}

bool MessageTree::writeReasonListToken(std::ostream& stream,
                                       const CompactReasonList& reasonList,
                                       const StringDirectory& reasonDirectory) {
    stream << reasonList.size() << '\n';
    for (const auto& reason : reasonList) {
        const auto messageText = reasonDirectory.get(reason.messageSlotIndex);
        if (!messageText.has_value()) {
            return false;
        }

        stream << std::quoted(*messageText) << ' '
               << reason.timestampMs << ' '
               << static_cast<std::uint32_t>(reason.fractionalDigits) << '\n';
    }
    return static_cast<bool>(stream);
}

bool MessageTree::readReasonListToken(std::istream& stream,
                                      CompactReasonList& reasonList,
                                      StringDirectory& reasonDirectory) {
    std::size_t count = 0U;
    if (!(stream >> count)) {
        return false;
    }

    reasonList.clear();
    reasonList.reserve(count);
    for (std::size_t idx = 0U; idx < count; ++idx) {
        std::string messageText{};
        if (!(stream >> std::quoted(messageText))) {
            return false;
        }

        std::int64_t timestampMs = 0;
        std::uint8_t fractionalDigits = 0U;
        if (!readReasonTimestampAndFraction(stream, timestampMs, fractionalDigits)) {
            return false;
        }

        reasonList.push_back(TreeNodeReasonEntry{
            .messageSlotIndex = reasonDirectory.add(messageText),
            .timestampMs = timestampMs,
            .fractionalDigits = fractionalDigits
        });
    }
    return true;
}

bool MessageTree::writeCompressedHistoryEntry(std::ostream& stream,
                                              const CompressedHistoryEntry& entry,
                                              const StringDirectory& reasonDirectory) {
    if (const auto* singleEntry = std::get_if<SingleHistoryEntry>(&entry.data)) {
        stream << "single\n";
        stream << singleEntry->timeMs << '\n';
        if (!writeValueToken(stream, singleEntry->value)) {
            return false;
        }
        return writeReasonListToken(stream, singleEntry->reason, reasonDirectory);
    }

    if (const auto* timeValueEntry = std::get_if<TimeValueHistoryEntry>(&entry.data)) {
        stream << "timeValue\n";
        stream << timeValueEntry->values.size() << '\n';
        for (const auto& timeValue : timeValueEntry->values) {
            stream << timeValue.first << '\n';
            if (!writeValueToken(stream, timeValue.second)) {
                return false;
            }
        }
        return writeReasonListToken(stream, timeValueEntry->reason, reasonDirectory);
    }

    if (const auto* timeEntry = std::get_if<TimeHistoryEntry>(&entry.data)) {
        stream << "time\n";
        if (!writeValueToken(stream, timeEntry->value)) {
            return false;
        }
        stream << timeEntry->timestamps.size() << '\n';
        for (const std::int64_t timestamp : timeEntry->timestamps) {
            stream << timestamp << '\n';
        }
        return writeReasonListToken(stream, timeEntry->reason, reasonDirectory);
    }

    const auto* intervalEntry = std::get_if<IntervalHistoryEntry>(&entry.data);
    if (intervalEntry == nullptr) {
        return false;
    }

    stream << "interval\n";
    stream << intervalEntry->amount << '\n';
    if (!writeValueToken(stream, intervalEntry->value)) {
        return false;
    }
    if (!writeReasonListToken(stream, intervalEntry->reason, reasonDirectory)) {
        return false;
    }
    stream << intervalEntry->firstTimeMs << '\n';
    stream << intervalEntry->lastTimeMs << '\n';
    return static_cast<bool>(stream);
}

bool MessageTree::readCompressedHistoryEntry(std::istream& stream,
                                             CompressedHistoryEntry& entry,
                                             StringDirectory& reasonDirectory) {
    std::string historyType{};
    if (!(stream >> historyType)) {
        return false;
    }

    if (historyType == "single") {
        return readSingleHistoryEntry(stream, entry, reasonDirectory);
    }
    if (historyType == "timeValue") {
        return readTimeValueHistoryEntry(stream, entry, reasonDirectory);
    }
    if (historyType == "time") {
        return readTimeHistoryEntry(stream, entry, reasonDirectory);
    }
    if (historyType == "interval") {
        return readIntervalHistoryEntry(stream, entry, reasonDirectory);
    }
    return false;
}

bool MessageTree::readSingleHistoryEntry(std::istream& stream,
                                         CompressedHistoryEntry& entry,
                                         StringDirectory& reasonDirectory) {
    SingleHistoryEntry singleEntry{};
    if (!(stream >> singleEntry.timeMs)) {
        return false;
    }
    if (!readValueToken(stream, singleEntry.value)) {
        return false;
    }
    if (!readReasonListToken(stream, singleEntry.reason, reasonDirectory)) {
        return false;
    }
    entry.data = std::move(singleEntry);
    return true;
}

bool MessageTree::readTimeValueHistoryEntry(std::istream& stream,
                                            CompressedHistoryEntry& entry,
                                            StringDirectory& reasonDirectory) {
    TimeValueHistoryEntry timeValueEntry{};
    std::size_t valueCount = 0U;
    if (!(stream >> valueCount)) {
        return false;
    }

    timeValueEntry.values.reserve(valueCount);
    for (std::size_t idx = 0U; idx < valueCount; ++idx) {
        std::int64_t timeMs = 0;
        if (!(stream >> timeMs)) {
            return false;
        }

        Value value{};
        if (!readValueToken(stream, value)) {
            return false;
        }
        timeValueEntry.values.emplace_back(timeMs, std::move(value));
    }

    if (!readReasonListToken(stream, timeValueEntry.reason, reasonDirectory)) {
        return false;
    }
    entry.data = std::move(timeValueEntry);
    return true;
}

bool MessageTree::readTimeHistoryEntry(std::istream& stream,
                                       CompressedHistoryEntry& entry,
                                       StringDirectory& reasonDirectory) {
    TimeHistoryEntry timeEntry{};
    if (!readValueToken(stream, timeEntry.value)) {
        return false;
    }

    std::size_t timestampCount = 0U;
    if (!(stream >> timestampCount)) {
        return false;
    }
    timeEntry.timestamps.reserve(timestampCount);
    for (std::size_t idx = 0U; idx < timestampCount; ++idx) {
        std::int64_t timestamp = 0;
        if (!(stream >> timestamp)) {
            return false;
        }
        timeEntry.timestamps.push_back(timestamp);
    }

    if (!readReasonListToken(stream, timeEntry.reason, reasonDirectory)) {
        return false;
    }
    entry.data = std::move(timeEntry);
    return true;
}

bool MessageTree::readIntervalHistoryEntry(std::istream& stream,
                                           CompressedHistoryEntry& entry,
                                           StringDirectory& reasonDirectory) {
    IntervalHistoryEntry intervalEntry{};
    if (!(stream >> intervalEntry.amount)) {
        return false;
    }
    if (!readValueToken(stream, intervalEntry.value)) {
        return false;
    }
    if (!readReasonListToken(stream, intervalEntry.reason, reasonDirectory)) {
        return false;
    }
    if (!(stream >> intervalEntry.firstTimeMs)) {
        return false;
    }
    if (!(stream >> intervalEntry.lastTimeMs)) {
        return false;
    }
    entry.data = std::move(intervalEntry);
    return true;
}

bool MessageTree::writeCompressedTreeNode(std::ostream& stream, const TreeNode& node) const {
    stream << std::quoted(node.topicPath) << '\n';
    stream << node.children.size() << '\n';
    stream << (node.data != nullptr ? 1 : 0) << '\n';

    if (node.data != nullptr) {
        stream << node.data->timeMs << '\n';
        if (!writeValueToken(stream, node.data->value)) {
            return false;
        }
        if (!writeReasonListToken(stream, node.data->reason, node.data->reasonDirectory)) {
            return false;
        }

        stream << node.data->compressedHistory.size() << '\n';
        for (const auto& historyEntry : node.data->compressedHistory) {
            if (!writeCompressedHistoryEntry(stream, historyEntry, node.data->reasonDirectory)) {
                return false;
            }
        }
    }

    for (const auto& child : node.children) {
        stream << std::quoted(child.first) << '\n';
        if (!writeCompressedTreeNode(stream, child.second)) {
            return false;
        }
    }

    return static_cast<bool>(stream);
}

bool MessageTree::readCompressedTreeNode(std::istream& stream, TreeNode& node) {
    if (!(stream >> std::quoted(node.topicPath))) {
        return false;
    }

    std::size_t childCount = 0U;
    if (!(stream >> childCount)) {
        return false;
    }

    int hasDataValue = 0;
    if (!(stream >> hasDataValue)) {
        return false;
    }

    if (hasDataValue != 0) {
        node.data = std::make_unique<NodeData>();
    } else {
        node.data.reset();
    }

    if (node.data != nullptr) {
        if (!(stream >> node.data->timeMs)) {
            return false;
        }
        if (!readValueToken(stream, node.data->value)) {
            return false;
        }
        if (!readReasonListToken(stream, node.data->reason, node.data->reasonDirectory)) {
            return false;
        }

        std::size_t historyCount = 0U;
        if (!(stream >> historyCount)) {
            return false;
        }

        node.data->compressedHistory.clear();
        for (std::size_t idx = 0U; idx < historyCount; ++idx) {
            CompressedHistoryEntry entry{};
            if (!readCompressedHistoryEntry(stream, entry, node.data->reasonDirectory)) {
                return false;
            }
            node.data->compressedHistory.push_back(std::move(entry));
        }
    }

    node.children.clear();
    node.children.reserve(childCount);
    for (std::size_t idx = 0U; idx < childCount; ++idx) {
        std::string childSegment{};
        if (!(stream >> std::quoted(childSegment))) {
            return false;
        }

        TreeNode childNode{};
        if (!readCompressedTreeNode(stream, childNode)) {
            return false;
        }
        node.children.emplace_back(std::move(childSegment), std::move(childNode));
    }

    return true;
}

void MessageTree::compactReasonDirectory(NodeData& data) {
    StringDirectory newDirectory{};
    std::unordered_map<slotIndex_t, slotIndex_t> remap{};

    const StringDirectory oldDirectory = data.reasonDirectory;
    const auto remapReasonList = [&oldDirectory, &newDirectory, &remap](CompactReasonList& reasonList) {
        for (auto& reasonEntry : reasonList) {
            const auto existing = remap.find(reasonEntry.messageSlotIndex);
            if (existing != remap.end()) {
                reasonEntry.messageSlotIndex = existing->second;
                continue;
            }

            const auto messageText = oldDirectory.get(reasonEntry.messageSlotIndex);
            if (!messageText.has_value()) {
                throw std::runtime_error{"MessageTree reason slot remap failed"};
            }

            const slotIndex_t newSlotIndex = newDirectory.add(*messageText);
            remap.emplace(reasonEntry.messageSlotIndex, newSlotIndex);
            reasonEntry.messageSlotIndex = newSlotIndex;
        }
    };

    remapReasonList(data.reason);
    for (auto& historyEntry : data.compressedHistory) {
        std::visit(
            [&](auto& typedEntry) {
                using EntryType = std::decay_t<decltype(typedEntry)>;
                if constexpr (std::is_same_v<EntryType, SingleHistoryEntry>) {
                    remapReasonList(typedEntry.reason);
                } else {
                    remapReasonList(typedEntry.reason);
                }
            },
            historyEntry.data);
    }

    data.reasonDirectory = std::move(newDirectory);
}

std::int64_t MessageTree::nowMilliseconds() const {
    return config_.nowMillisecondsProvider();
}

TreeNode* MessageTree::ensurePath(const std::string& topic) {
    TreeNode* current = &root_;
    std::string currentPath{};

    for (const auto& segment : splitTopic(topic)) {
        if (!currentPath.empty()) {
            currentPath.push_back('/');
        }
        currentPath += segment;

        const auto childIndex = findChildIndex(*current, segment);
        if (!childIndex.has_value()) {
            TreeNode child{};
            child.topicPath = currentPath;
            current->children.emplace_back(segment, std::move(child));
            const std::size_t newIndex = current->children.size() - 1U;
            current = &current->children[newIndex].second;
            continue;
        }

        current = &current->children[*childIndex].second;
    }

    return current;
}

const TreeNode* MessageTree::findPath(const std::string& topic) const {
    const TreeNode* current = &root_;
    for (const auto& segment : splitTopic(topic)) {
        const auto childIndex = findChildIndex(*current, segment);
        if (!childIndex.has_value()) {
            return nullptr;
        }
        current = &current->children[*childIndex].second;
    }
    return current;
}

std::optional<std::size_t>
MessageTree::findChildIndex(const TreeNode& node, const std::string& segment) {
    for (std::size_t index = 0U; index < node.children.size(); ++index) {
        if (node.children[index].first == segment) {
            return index;
        }
    }
    return std::nullopt;
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

void MessageTree::collectSection(const TreeNode& node,
                                 std::uint32_t maxDepth,
                                 std::uint32_t currentDepth,
                                 bool includeHistory,
                                 bool includeReason,
                                 std::vector<MessageTreeNode>& output) const {
    if (node.data != nullptr) {
        MessageTreeNode result{};
        result.topic = node.topicPath;
        result.timeMs = node.data->timeMs;
        result.value = node.data->value;
        if (includeReason) {
            result.setReasonList(toReasonList(node.data->reason, node.data->reasonDirectory));
        } else {
            result.clearReason();
        }
        result.setHistoryEntries(includeHistory
            ? decompressHistory(node.data->compressedHistory, node.data->reasonDirectory, true)
            : std::vector<MessageTreeHistoryEntry>{});
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
                                 const MessageSnapshot& snapshot) {
    if (snapshot.timeMs.has_value() && current.timeMs != *snapshot.timeMs) {
        return false;
    }

    if (current.value != snapshot.value) {
        return false;
    }

    if (snapshot.reason.empty()) {
        return true;
    }

    return reasonListsEqual(current.reason(), snapshot.reason);
}

std::size_t MessageTree::cleanupNode(TreeNode& node, std::int64_t cutoffMs) {
    std::size_t removed = 0U;

    for (auto iter = node.children.begin(); iter != node.children.end();) {
        removed += cleanupNode(iter->second, cutoffMs);
        if (iter->second.data == nullptr && iter->second.children.empty()) {
            iter = node.children.erase(iter);
            continue;
        }
        ++iter;
    }

    if (node.data != nullptr && node.data->timeMs < cutoffMs) {
        node.data.reset();
        removed += 1U;
    }

    return removed;
}

} // namespace yaha
