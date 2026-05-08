#include "yaha/message_store/message_tree.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace yaha {

namespace {

constexpr int k_millis_per_second{1000};
constexpr int k_seconds_per_minute{60};
constexpr int k_minutes_per_hour{60};
constexpr int k_hours_per_day{24};
constexpr int k_leap_cycle_divisor{4};
constexpr int k_century_divisor{100};
constexpr int k_quad_century_divisor{400};
constexpr int k_month_count{12};
constexpr int k_min_month{1};
constexpr int k_february_month{2};
constexpr int k_february_days_in_leap_year{29};
constexpr int k_max_hour{23};
constexpr int k_max_minute_or_second{59};
constexpr int k_iso_utc_hour_limit{23};
constexpr int k_iso_utc_minute_limit{59};
constexpr int k_decimal_base{10};
constexpr std::size_t k_iso_min_length{20U};
constexpr std::size_t k_fraction_scale_digits{3U};
constexpr std::size_t k_two_digits{2U};
constexpr std::size_t k_iso_year_start{0U};
constexpr std::size_t k_iso_year_digits{4U};
constexpr std::size_t k_iso_month_start{5U};
constexpr std::size_t k_iso_day_start{8U};
constexpr std::size_t k_iso_hour_start{11U};
constexpr std::size_t k_iso_minute_start{14U};
constexpr std::size_t k_iso_second_start{17U};
constexpr std::size_t k_iso_after_second_start{19U};
constexpr std::size_t k_iso_month_separator_pos{4U};
constexpr std::size_t k_iso_day_separator_pos{7U};
constexpr std::size_t k_iso_date_time_separator_pos{10U};
constexpr std::size_t k_iso_hour_separator_pos{13U};
constexpr std::size_t k_iso_minute_separator_pos{16U};
constexpr int k_days_per_year{365};
constexpr int k_days_per_era{146097};
constexpr int k_unix_epoch_civil_offset_days{719468};
constexpr int k_day_formula_factor{153};
constexpr int k_day_formula_add{2};
constexpr int k_day_formula_divisor{5};
constexpr int k_month_adjust_after_february{-3};
constexpr int k_month_adjust_before_or_equal_february{9};

constexpr std::array<int, k_month_count> k_days_per_month{
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

struct ParsedIsoTimestamp {
    int year{0};
    int month{0};
    int day{0};
    int hour{0};
    int minute{0};
    int second{0};
    int millis{0};
    int timezoneOffsetMinutes{0};
};

bool isLeapYear(const int year) {
    return (year % k_leap_cycle_divisor == 0 && year % k_century_divisor != 0) ||
           year % k_quad_century_divisor == 0;
}

int daysInMonth(const int year, const int month) {
    if (month < k_min_month || month > k_month_count) {
        return 0;
    }

    if (month == k_february_month && isLeapYear(year)) {
        return k_february_days_in_leap_year;
    }

    return k_days_per_month[static_cast<std::size_t>(month - k_min_month)];
}

bool parseFixedDigits(const std::string& text,
                      const std::size_t start,
                      const std::size_t amount,
                      int& parsedValue) {
    if (start + amount > text.size()) {
        return false;
    }

    int value = 0;
    for (std::size_t idx = 0U; idx < amount; ++idx) {
        const char chr = text[start + idx];
        if (std::isdigit(static_cast<unsigned char>(chr)) == 0) {
            return false;
        }
        value = (value * k_decimal_base) + (chr - '0');
    }

    parsedValue = value;
    return true;
}

std::int64_t daysFromCivil(int year, const unsigned int month, const unsigned int day) {
    year -= month <= 2U ? 1 : 0;
    const int era = (year >= 0 ? year : year - (k_quad_century_divisor - 1)) / k_quad_century_divisor;
    const unsigned int yearOfEra = static_cast<unsigned int>(year - (era * k_quad_century_divisor));
    const int monthForFormula = static_cast<int>(month);
    const int monthAdjustment = monthForFormula > k_february_month
        ? k_month_adjust_after_february
        : k_month_adjust_before_or_equal_february;
    const unsigned int dayOfYear =
        static_cast<unsigned int>((k_day_formula_factor * (monthForFormula + monthAdjustment) +
                                   k_day_formula_add) / k_day_formula_divisor) +
        day - 1U;
    const unsigned int dayOfEra =
        (yearOfEra * static_cast<unsigned int>(k_days_per_year)) +
        (yearOfEra / static_cast<unsigned int>(k_leap_cycle_divisor)) -
        (yearOfEra / static_cast<unsigned int>(k_century_divisor)) +
        dayOfYear;
    return (static_cast<std::int64_t>(era) * k_days_per_era) +
           static_cast<std::int64_t>(dayOfEra) -
           k_unix_epoch_civil_offset_days;
}

bool hasIsoDateTimeLayout(const std::string& timestampText) {
    return timestampText[k_iso_month_separator_pos] == '-' &&
           timestampText[k_iso_day_separator_pos] == '-' &&
           timestampText[k_iso_date_time_separator_pos] == 'T' &&
           timestampText[k_iso_hour_separator_pos] == ':' &&
           timestampText[k_iso_minute_separator_pos] == ':';
}

bool parseIsoDateTimeFields(const std::string& timestampText, ParsedIsoTimestamp& parsed) {
    if (!parseFixedDigits(timestampText, k_iso_year_start, k_iso_year_digits, parsed.year) ||
        !parseFixedDigits(timestampText, k_iso_month_start, k_two_digits, parsed.month) ||
        !parseFixedDigits(timestampText, k_iso_day_start, k_two_digits, parsed.day) ||
        !parseFixedDigits(timestampText, k_iso_hour_start, k_two_digits, parsed.hour) ||
        !parseFixedDigits(timestampText, k_iso_minute_start, k_two_digits, parsed.minute) ||
        !parseFixedDigits(timestampText, k_iso_second_start, k_two_digits, parsed.second)) {
        return false;
    }

    if (parsed.month < k_min_month || parsed.month > k_month_count) {
        return false;
    }
    if (parsed.day < 1 || parsed.day > daysInMonth(parsed.year, parsed.month)) {
        return false;
    }

    if (parsed.hour < 0 || parsed.hour > k_max_hour ||
        parsed.minute < 0 || parsed.minute > k_max_minute_or_second ||
        parsed.second < 0 || parsed.second > k_max_minute_or_second) {
        return false;
    }

    return true;
}

bool parseIsoFractionMilliseconds(const std::string& timestampText,
                                  std::size_t& cursor,
                                  ParsedIsoTimestamp& parsed) {
    if (cursor >= timestampText.size() || timestampText[cursor] != '.') {
        return true;
    }

    cursor += 1U;
    const std::size_t fractionStart = cursor;
    while (cursor < timestampText.size() &&
           std::isdigit(static_cast<unsigned char>(timestampText[cursor])) != 0) {
        cursor += 1U;
    }

    const std::size_t fractionDigits = cursor - fractionStart;
    if (fractionDigits == 0U) {
        return false;
    }

    parsed.millis = 0;
    for (std::size_t idx = 0U; idx < k_fraction_scale_digits; ++idx) {
        parsed.millis *= k_decimal_base;
        if (idx < fractionDigits) {
            parsed.millis += static_cast<int>(timestampText[fractionStart + idx] - '0');
        }
    }

    return true;
}

bool parseIsoTimezoneOffset(const std::string& timestampText,
                            std::size_t& cursor,
                            ParsedIsoTimestamp& parsed) {
    if (cursor >= timestampText.size()) {
        return false;
    }

    if (timestampText[cursor] == 'Z') {
        cursor += 1U;
        parsed.timezoneOffsetMinutes = 0;
        return true;
    }

    if (timestampText[cursor] != '+' && timestampText[cursor] != '-') {
        return false;
    }

    const bool positiveOffset = timestampText[cursor] == '+';
    cursor += 1U;

    int offsetHour = 0;
    int offsetMinute = 0;
    if (!parseFixedDigits(timestampText, cursor, k_two_digits, offsetHour)) {
        return false;
    }
    cursor += k_two_digits;

    if (cursor >= timestampText.size() || timestampText[cursor] != ':') {
        return false;
    }
    cursor += 1U;

    if (!parseFixedDigits(timestampText, cursor, k_two_digits, offsetMinute)) {
        return false;
    }
    cursor += k_two_digits;

    if (offsetHour > k_iso_utc_hour_limit || offsetMinute > k_iso_utc_minute_limit) {
        return false;
    }

    parsed.timezoneOffsetMinutes = offsetHour * k_minutes_per_hour + offsetMinute;
    if (!positiveOffset) {
        parsed.timezoneOffsetMinutes = -parsed.timezoneOffsetMinutes;
    }

    return true;
}

bool tryParseIsoTimestampMilliseconds(const std::string& timestampText,
                                      std::int64_t& outMilliseconds) {
    if (timestampText.size() < k_iso_min_length || !hasIsoDateTimeLayout(timestampText)) {
        return false;
    }

    ParsedIsoTimestamp parsed{};
    if (!parseIsoDateTimeFields(timestampText, parsed)) {
        return false;
    }

    std::size_t cursor = k_iso_after_second_start;
    if (!parseIsoFractionMilliseconds(timestampText, cursor, parsed)) {
        return false;
    }

    if (!parseIsoTimezoneOffset(timestampText, cursor, parsed)) {
        return false;
    }
    if (cursor != timestampText.size()) {
        return false;
    }

    const std::int64_t days = daysFromCivil(parsed.year,
                                            static_cast<unsigned int>(parsed.month),
                                            static_cast<unsigned int>(parsed.day));
    const std::int64_t totalHours = (days * k_hours_per_day) + parsed.hour;
    const std::int64_t totalMinutes = (totalHours * k_minutes_per_hour) + parsed.minute;
    const std::int64_t totalSeconds = (totalMinutes * k_seconds_per_minute) + parsed.second;

    outMilliseconds = (totalSeconds * k_millis_per_second) + parsed.millis -
        (static_cast<std::int64_t>(parsed.timezoneOffsetMinutes) *
         k_seconds_per_minute *
         k_millis_per_second);
    return true;
}

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

    std::int64_t effectiveTimeMs = nowMilliseconds();
    const auto& reasons = message.reason();
    if (!reasons.empty()) {
        std::int64_t reasonTimeMs = 0;
        if (tryParseIsoTimestampMilliseconds(reasons.front().timestamp, reasonTimeMs)) {
            effectiveTimeMs = reasonTimeMs;
        }
    }

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
