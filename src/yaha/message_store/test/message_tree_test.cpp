#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "yaha/message/message.h"
#include "yaha/message_store/message_tree.h"

namespace {

constexpr std::int64_t k_initial_now_ms{1000};
constexpr std::int64_t k_tick_ms{1000};
constexpr std::int64_t k_reason_timestamp_fallback_clock_ms{999999};
constexpr std::int64_t k_invalid_reason_fallback_clock_ms{7777};
constexpr std::int64_t k_reason_timestamp_expected_ms{1250};
constexpr std::int64_t k_positive_offset_expected_ms{1800000};
constexpr std::int64_t k_negative_offset_expected_ms{3601500};
constexpr std::int64_t k_invalid_timezone_fallback_clock_ms{8888};
constexpr std::int64_t k_invalid_fraction_fallback_clock_ms{9999};
constexpr std::int64_t k_time_zero_ms{0};
constexpr std::int64_t k_time_half_second_ms{500};
constexpr std::int64_t k_time_one_second_ms{1000};
constexpr std::int64_t k_time_two_seconds_ms{2000};
constexpr std::int64_t k_time_three_seconds_ms{3000};
constexpr std::int64_t k_time_four_seconds_ms{4000};
constexpr std::int64_t k_time_four_point_five_seconds_ms{4500};
constexpr std::int64_t k_time_five_seconds_ms{5000};
constexpr std::int64_t k_time_nine_seconds_ms{9000};
constexpr std::int64_t k_time_ten_seconds_ms{10000};
constexpr std::int64_t k_time_thirteen_seconds_ms{13000};
constexpr std::int64_t k_time_fifteen_seconds_ms{15000};
constexpr std::int64_t k_time_twenty_seconds_ms{20000};
constexpr std::int64_t k_time_thirty_seconds_ms{30000};
constexpr std::int64_t k_time_forty_seconds_ms{40000};
constexpr std::int64_t k_time_fifty_seconds_ms{50000};
constexpr std::int64_t k_time_sixty_seconds_ms{60000};
constexpr std::int64_t k_time_one_hundred_one_seconds_ms{101000};
constexpr std::int64_t k_time_one_hundred_two_seconds_ms{102000};
constexpr double k_temperature_before{21.0};
constexpr double k_temperature_after{22.0};
constexpr double k_value_two{2.0};
constexpr double k_value_three{3.0};
constexpr double k_value_four{4.0};
constexpr double k_value_five{5.0};
constexpr double k_value_seven{7.0};
constexpr double k_light_on_time_seconds{1200.0};
constexpr double k_interval_upper_bound_factor{1.01};
constexpr double k_interval_lower_bound_factor{0.99};
constexpr std::uint32_t k_length_for_time_value_only{10U};
constexpr int k_history_last_step{6};
constexpr int k_count_integrity_steps{8};
constexpr int k_decimal_base{10};

struct FakeClock {
    std::int64_t nowMs{k_initial_now_ms};
};

yaha::MessageTree makeTree(
    FakeClock& clock,
    std::uint32_t maxHistoryLength = yaha::MessageTreeConfig::k_default_max_history_length,
    std::uint32_t historyHysterese = yaha::MessageTreeConfig::k_default_history_hysterese,
    std::uint32_t maxValuesPerHistoryEntry = yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
    std::uint32_t lengthForFurtherCompression = yaha::MessageTreeConfig::k_default_length_for_further_compression,
    double upperBoundFactor = yaha::MessageTreeConfig::k_default_upper_bound_factor,
    std::uint32_t upperBoundAddInMilliseconds =
        yaha::MessageTreeConfig::k_default_upper_bound_add_in_milliseconds,
    double lowerBoundFactor = yaha::MessageTreeConfig::k_default_lower_bound_factor,
    std::uint32_t lowerBoundSubInMilliseconds =
        yaha::MessageTreeConfig::k_default_lower_bound_sub_in_milliseconds) {
    yaha::MessageTreeConfig config{};
    config.maxHistoryLength = maxHistoryLength;
    config.historyHysterese = historyHysterese;
    config.maxValuesPerHistoryEntry = maxValuesPerHistoryEntry;
    config.lengthForFurtherCompression = lengthForFurtherCompression;
    config.upperBoundFactor = upperBoundFactor;
    config.upperBoundAddInMilliseconds = upperBoundAddInMilliseconds;
    config.lowerBoundFactor = lowerBoundFactor;
    config.lowerBoundSubInMilliseconds = lowerBoundSubInMilliseconds;
    config.nowMillisecondsProvider = [&clock]() {
        return clock.nowMs;
    };
    return yaha::MessageTree{config};
}

bool containsTopic(const std::vector<yaha::MessageTreeNode>& nodes,
                   const std::string& topic) {
    return std::any_of(nodes.begin(), nodes.end(),
                       [&topic](const yaha::MessageTreeNode& node) {
        return node.topic == topic;
    });
}

bool historyHasUniqueTimestamps(const std::vector<yaha::MessageTreeHistoryEntry>& history) {
    std::unordered_set<std::int64_t> seen{};
    seen.reserve(history.size());
    for (const auto& entry : history) {
        if (!seen.insert(entry.timeMs).second) {
            return false;
        }
    }
    return true;
}

yaha::Message makeReasonedMessage(const std::string& topic,
                                  const yaha::Value& value,
                                  const std::string& reasonMessage,
                                  const std::string& reasonTimestamp) {
    yaha::Message message{topic, value};
    message.addReason(reasonMessage, reasonTimestamp);
    return message;
}

void requireTotalEntryCount(const yaha::MessageTree& tree,
                            const std::string& topic,
                            const std::size_t insertedMessages) {
    const auto representedHistoryCount = [](const yaha::MessageTreeHistoryEntry& entry) {
        static const std::string k_interval_prefix{"regular update, amount: "};

        if (!entry.reason.empty() && entry.reason.front().message.rfind(k_interval_prefix, 0U) == 0U) {
            const std::string amountText = entry.reason.front().message.substr(k_interval_prefix.size());
            if (!amountText.empty() &&
                std::all_of(amountText.begin(), amountText.end(), [](unsigned char character) {
                    return std::isdigit(character) != 0;
                })) {
                return static_cast<std::size_t>(std::strtoul(amountText.c_str(), nullptr, k_decimal_base));
            }
        }

        return static_cast<std::size_t>(1U);
    };

    const auto nodes = tree.getSection(topic, 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    std::size_t logicalMessageCount = 1U;
    for (const auto& entry : nodes.front().history) {
        logicalMessageCount += representedHistoryCount(entry);
    }
    REQUIRE(logicalMessageCount == insertedMessages);
}

} // namespace

TEST_CASE("add_data_creates_node_and_get_section_returns_it", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"home/living/light", std::string{"on"}});

    const auto nodes = tree.getSection("", 3U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "home/living/light");
    REQUIRE(std::get<std::string>(nodes.front().value) == "on");
}

TEST_CASE("add_data_updates_move_previous_value_into_history", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"home/living/temp", k_temperature_before});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"home/living/temp", k_temperature_after});

    const auto nodes = tree.getSection("home/living/temp", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(std::get<double>(nodes.front().value) == k_temperature_after);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(std::get<double>(nodes.front().history.front().value) == k_temperature_before);
}

TEST_CASE("add_data_prefers_first_reason_timestamp_when_valid_iso", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_reason_timestamp_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/with_reason", std::string{"v"}};
    message.addReason("older", "1970-01-01T00:00:00.500Z");
    message.addReason("newest", "1970-01-01T00:00:01.250Z");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/with_reason", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_reason_timestamp_expected_ms);
}

TEST_CASE("add_data_falls_back_to_clock_when_first_reason_timestamp_invalid", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_invalid_reason_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/with_invalid_reason", std::string{"v"}};
    message.addReason("older-valid", "1970-01-01T00:00:01.250Z");
    message.addReason("newest-invalid", "not-an-iso-timestamp");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/with_invalid_reason", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_invalid_reason_fallback_clock_ms);
}

TEST_CASE("add_data_parses_reason_timestamp_with_positive_timezone_offset", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_invalid_timezone_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/offset_positive", std::string{"v"}};
    message.addReason("offset", "1970-01-01T01:30:00+01:00");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/offset_positive", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_positive_offset_expected_ms);
}

TEST_CASE("add_data_parses_reason_timestamp_with_negative_offset_and_fraction", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_invalid_timezone_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/offset_negative", std::string{"v"}};
    message.addReason("offset", "1970-01-01T00:00:01.5-01:00");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/offset_negative", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_negative_offset_expected_ms);
}

TEST_CASE("add_data_falls_back_to_clock_for_invalid_reason_timezone_format", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_invalid_timezone_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/invalid_tz", std::string{"v"}};
    message.addReason("bad", "1970-01-01T00:00:01+0100");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/invalid_tz", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_invalid_timezone_fallback_clock_ms);
}

TEST_CASE("add_data_falls_back_to_clock_for_invalid_reason_fraction_format", "[message_store]") {
    FakeClock clock{};
    clock.nowMs = k_invalid_fraction_fallback_clock_ms;
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message message{"sensor/invalid_fraction", std::string{"v"}};
    message.addReason("bad", "1970-01-01T00:00:01.Z");

    tree.addData(message);

    const auto nodes = tree.getSection("sensor/invalid_fraction", 0U, false, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().timeMs == k_invalid_fraction_fallback_clock_ms);
}

TEST_CASE("history_is_trimmed_with_hysteresis", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock, 3U, 1U);

    tree.addData(makeReasonedMessage("sensor/value", 1.0, "step-1", "2026-01-01T00:00:00Z"));
    for (int step = 2; step <= k_history_last_step; ++step) {
        clock.nowMs += k_tick_ms;
        const std::string reasonMessage = "step-" + std::to_string(step);
        tree.addData(makeReasonedMessage("sensor/value", static_cast<double>(step), reasonMessage,
                                         "2026-01-01T00:00:00Z"));
    }

    const auto nodes = tree.getSection("sensor/value", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(std::get<double>(nodes.front().history[0].value) == k_value_five);
    REQUIRE(std::get<double>(nodes.front().history[1].value) == k_value_four);
}

TEST_CASE("history_compresses_repeated_equal_values", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      2U);

    tree.addData(yaha::Message{"sensor/equal", std::string{"steady"}});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/equal", std::string{"steady"}});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/equal", std::string{"steady"}});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/equal", std::string{"steady"}});

    const auto nodes = tree.getSection("sensor/equal", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 3U);
    REQUIRE(std::get<std::string>(nodes.front().history[0].value) == "steady");
    REQUIRE(std::get<std::string>(nodes.front().history[1].value) == "steady");
    REQUIRE(nodes.front().history[2].reason.empty());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_single_compression_keeps_reasoned_entries_separate", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/compression_single", 1.0, "r1", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/compression_single", k_value_two, "r2", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_single", k_value_three, "r3", "1970-01-01T00:00:02.000Z"));

    const auto nodes = tree.getSection("sensor/compression_single", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(std::get<double>(nodes.front().history[0].value) == k_value_two);
    REQUIRE(std::get<double>(nodes.front().history[1].value) == 1.0);
    REQUIRE(nodes.front().history[0].reason.size() == 1U);
    REQUIRE(nodes.front().history[1].reason.size() == 1U);
    REQUIRE(nodes.front().history[0].reason[0].message == "r2");
    REQUIRE(nodes.front().history[1].reason[0].message == "r1");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_time_value_compression_merges_value_sequence", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time_value", 1.0, "source", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time_value", k_value_two, "source", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time_value", k_value_three, "source", "1970-01-01T00:00:02.000Z"));

    const auto nodes = tree.getSection("sensor/compression_time_value", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(nodes.front().history[0].reason.empty());
    REQUIRE(nodes.front().history[1].reason.size() == 1U);
    REQUIRE(nodes.front().history[1].reason[0].message == "source");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_time_compression_merges_identical_values_without_interval", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U,
                                      k_interval_upper_bound_factor,
                                      0U,
                                      k_interval_lower_bound_factor,
                                      0U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time", std::string{"steady"}, "source", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time", std::string{"steady"}, "source", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_four_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time", std::string{"steady"}, "source", "1970-01-01T00:00:04.000Z"));
    clock.nowMs = k_time_nine_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_time", std::string{"steady"}, "source", "1970-01-01T00:00:09.000Z"));

    const auto nodes = tree.getSection("sensor/compression_time", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 3U);
    REQUIRE(nodes.front().history[0].reason.empty());
    REQUIRE(nodes.front().history[1].reason.empty());
    REQUIRE(nodes.front().history[2].reason.size() == 1U);
    REQUIRE(nodes.front().history[2].reason[0].message == "source");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_interval_compression_merges_regular_updates", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/compression_interval", k_value_five, "source", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/compression_interval", k_value_five, "source", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_interval", k_value_five, "source", "1970-01-01T00:00:02.000Z"));
    clock.nowMs = k_time_three_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_interval", k_value_five, "source", "1970-01-01T00:00:03.000Z"));
    clock.nowMs = k_time_four_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/compression_interval", k_value_five, "source", "1970-01-01T00:00:04.000Z"));

    const auto nodes = tree.getSection("sensor/compression_interval", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(nodes.front().history[0].reason.size() == 2U);
    REQUIRE(nodes.front().history[0].reason[0].message == "regular update, amount: 4");
    REQUIRE(nodes.front().history[0].reason[1].message == "source");
}

TEST_CASE("history_single_entry_preserves_reason", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(makeReasonedMessage("sensor/single", 1.0, "initial", "2026-01-01T00:00:00Z"));
    clock.nowMs += k_tick_ms;
    tree.addData(makeReasonedMessage("sensor/single", k_value_two, "updated", "2026-01-01T00:00:01Z"));

    const auto nodes = tree.getSection("sensor/single", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(std::get<double>(nodes.front().history[0].value) == 1.0);
    REQUIRE(nodes.front().history[0].reason.size() == 1U);
    REQUIRE(nodes.front().history[0].reason[0].message == "initial");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_time_value_entry_keeps_oldest_reason_only", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(makeReasonedMessage("sensor/time_value", 1.0, "source", "2026-01-01T00:00:00Z"));
    clock.nowMs += k_tick_ms;
    tree.addData(makeReasonedMessage("sensor/time_value", k_value_two, "source", "2026-01-01T00:00:01Z"));
    clock.nowMs += k_tick_ms;
    tree.addData(makeReasonedMessage("sensor/time_value", k_value_three, "source", "2026-01-01T00:00:02Z"));

    const auto nodes = tree.getSection("sensor/time_value", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(std::get<double>(nodes.front().history[0].value) == k_value_two);
    REQUIRE(std::get<double>(nodes.front().history[1].value) == 1.0);
    REQUIRE(nodes.front().history[0].reason.empty());
    REQUIRE(nodes.front().history[1].reason.size() == 1U);
    REQUIRE(nodes.front().history[1].reason[0].message == "source");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_time_entry_for_identical_values_same_reason", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/time", std::string{"steady"}, "origin", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/time", std::string{"steady"}, "origin", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_one_hundred_one_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/time", std::string{"steady"}, "origin", "1970-01-01T00:01:41.000Z"));
    clock.nowMs = k_time_one_hundred_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/time", std::string{"steady"}, "origin", "1970-01-01T00:01:42.000Z"));

    const auto nodes = tree.getSection("sensor/time", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 3U);
    REQUIRE(std::get<std::string>(nodes.front().history[0].value) == "steady");
    REQUIRE(std::get<std::string>(nodes.front().history[1].value) == "steady");
    REQUIRE(std::get<std::string>(nodes.front().history[2].value) == "steady");
    REQUIRE(nodes.front().history[0].reason.empty());
    REQUIRE(nodes.front().history[1].reason.empty());
    REQUIRE(nodes.front().history[2].reason.size() == 1U);
    REQUIRE(nodes.front().history[2].reason[0].message == "origin");
}

TEST_CASE("history_interval_entry_for_regular_updates", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/interval", k_value_five, "source", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/interval", k_value_five, "source", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval", k_value_five, "source", "1970-01-01T00:00:02.000Z"));
    clock.nowMs = k_time_three_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval", k_value_five, "source", "1970-01-01T00:00:03.000Z"));
    clock.nowMs = k_time_four_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval", k_value_five, "source", "1970-01-01T00:00:04.000Z"));

    const auto nodes = tree.getSection("sensor/interval", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(nodes.front().history[0].timeMs == k_time_three_seconds_ms);
    REQUIRE(nodes.front().history[0].reason.size() == 2U);
    REQUIRE(nodes.front().history[0].reason[0].message == "regular update, amount: 4");
    REQUIRE(nodes.front().history[0].reason[1].message == "source");
}

TEST_CASE("history_interval_entry_rejects_irregular_updates", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U,
                                      k_interval_upper_bound_factor,
                                      0U,
                                      k_interval_lower_bound_factor,
                                      0U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:00.000Z"));
    clock.nowMs = k_time_one_second_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:01.000Z"));
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:02.000Z"));
    clock.nowMs = k_time_three_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:03.000Z"));
    clock.nowMs = k_time_nine_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:09.000Z"));
    clock.nowMs = k_time_fifteen_seconds_ms;
    tree.addData(makeReasonedMessage("sensor/interval_break", k_value_seven, "source", "1970-01-01T00:00:15.000Z"));

    const auto nodes = tree.getSection("sensor/interval_break", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() >= 2U);
    REQUIRE(nodes.front().history[0].timeMs == k_time_nine_seconds_ms);
    REQUIRE(nodes.front().history[1].reason.empty() == false);
    REQUIRE(nodes.front().history[1].reason[0].message.find("regular update, amount:") == 0U);
}

TEST_CASE("history_single_entries_do_not_duplicate_timestamps", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      1U,
                                      yaha::MessageTreeConfig::k_default_length_for_further_compression);

    tree.addData(yaha::Message{"sensor/no_dup_single", 1.0});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_single", k_value_two});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_single", k_value_three});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_single", k_value_four});

    const auto nodes = tree.getSection("sensor/no_dup_single", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
}

TEST_CASE("history_time_value_entries_do_not_duplicate_timestamps", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    tree.addData(yaha::Message{"sensor/no_dup_time_value", 1.0});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time_value", k_value_two});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time_value", k_value_three});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time_value", k_value_four});
    clock.nowMs += k_tick_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time_value", k_value_five});

    const auto nodes = tree.getSection("sensor/no_dup_time_value", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
}

TEST_CASE("history_time_value_reason_timestamp_override_adds_one_history_entry_per_message", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    const std::string sharedTimestamp{"2026-01-01T00:00:00.000Z"};
    std::size_t addedMessages = 0U;

    auto addOneAndAssert = [&](const yaha::Value& value, std::int64_t nowMs) {
        clock.nowMs = nowMs;
        tree.addData(makeReasonedMessage("sensor/no_dup_reason_override", value, "source", sharedTimestamp));
        addedMessages += 1U;

        const auto nodes = tree.getSection("sensor/no_dup_reason_override", 0U, true, true);
        REQUIRE(nodes.size() == 1U);
        REQUIRE(nodes.front().history.size() == (addedMessages - 1U));
    };

    addOneAndAssert(1.0, k_time_zero_ms);
    addOneAndAssert(k_value_two, k_time_one_second_ms);
    addOneAndAssert(k_value_three, k_time_two_seconds_ms);
    addOneAndAssert(k_value_four, k_time_three_seconds_ms);
}

TEST_CASE("history_time_entries_do_not_duplicate_timestamps", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = 0;
    tree.addData(yaha::Message{"sensor/no_dup_time", std::string{"steady"}});
    clock.nowMs = k_time_one_second_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time", std::string{"steady"}});
    clock.nowMs = k_time_five_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time", std::string{"steady"}});
    clock.nowMs = k_time_nine_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time", std::string{"steady"}});
    clock.nowMs = k_time_thirteen_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_time", std::string{"steady"}});

    const auto nodes = tree.getSection("sensor/no_dup_time", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
}

TEST_CASE("history_interval_entries_do_not_duplicate_timestamps", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = 0;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_ten_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_twenty_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_thirty_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_forty_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_fifty_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});
    clock.nowMs = k_time_sixty_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_interval", 1.0});

    const auto nodes = tree.getSection("sensor/no_dup_interval", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
}

TEST_CASE("history_time_to_interval_transition_does_not_duplicate_timestamps", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = 0;
    tree.addData(yaha::Message{"sensor/no_dup_transition", std::string{"steady"}});
    clock.nowMs = k_time_one_second_ms;
    tree.addData(yaha::Message{"sensor/no_dup_transition", std::string{"steady"}});
    clock.nowMs = k_time_five_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_transition", std::string{"steady"}});
    clock.nowMs = k_time_nine_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_transition", std::string{"steady"}});
    clock.nowMs = k_time_thirteen_seconds_ms;
    tree.addData(yaha::Message{"sensor/no_dup_transition", std::string{"steady"}});

    const auto nodes = tree.getSection("sensor/no_dup_transition", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
}

TEST_CASE("history_short_regular_tail_keeps_latest_visible_update", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(yaha::Message{"sensor/missing_update", std::string{"steady"}});
    clock.nowMs = k_time_one_second_ms;
    tree.addData(yaha::Message{"sensor/missing_update", std::string{"steady"}});
    clock.nowMs = k_time_five_seconds_ms;
    tree.addData(yaha::Message{"sensor/missing_update", std::string{"steady"}});
    clock.nowMs = k_time_nine_seconds_ms;
    tree.addData(yaha::Message{"sensor/missing_update", std::string{"steady"}});

    const auto nodes = tree.getSection("sensor/missing_update", 0U, true, true);
    REQUIRE(nodes.size() == 1U);

    const bool containsLatestPrevious = std::any_of(
        nodes.front().history.begin(),
        nodes.front().history.end(),
        [](const yaha::MessageTreeHistoryEntry& entry) {
            return entry.timeMs == k_time_five_seconds_ms;
        });

    REQUIRE(containsLatestPrevious);
}

TEST_CASE("history_interval_entry_reports_latest_previous_timestamp", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    clock.nowMs = k_time_zero_ms;
    tree.addData(yaha::Message{"sensor/interval_visible_update", 1.0});
    clock.nowMs = k_time_one_second_ms;
    tree.addData(yaha::Message{"sensor/interval_visible_update", 1.0});
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(yaha::Message{"sensor/interval_visible_update", 1.0});
    clock.nowMs = k_time_three_seconds_ms;
    tree.addData(yaha::Message{"sensor/interval_visible_update", 1.0});
    clock.nowMs = k_time_four_seconds_ms;
    tree.addData(yaha::Message{"sensor/interval_visible_update", 1.0});

    const auto nodes = tree.getSection("sensor/interval_visible_update", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(nodes.front().history[0].timeMs == k_time_three_seconds_ms);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_no_duplicate_timestamp_pattern_search", "[message_store]") {
    const std::vector<std::vector<std::int64_t>> scenarios{
        {k_time_one_second_ms, k_time_four_seconds_ms, k_time_four_seconds_ms, k_time_four_seconds_ms},
        {k_time_one_second_ms, k_time_four_seconds_ms, k_time_four_seconds_ms, k_time_four_seconds_ms, k_time_four_seconds_ms},
        {k_time_one_second_ms, k_time_two_seconds_ms, k_time_two_seconds_ms, k_time_two_seconds_ms, k_time_two_seconds_ms},
        {k_time_one_second_ms, k_time_three_seconds_ms, k_time_three_seconds_ms, k_time_three_seconds_ms, k_time_three_seconds_ms},
        {k_time_half_second_ms, k_time_four_point_five_seconds_ms, k_time_four_point_five_seconds_ms, k_time_four_point_five_seconds_ms, k_time_four_point_five_seconds_ms},
        {k_time_one_second_ms, k_time_one_second_ms, k_time_three_seconds_ms, k_time_three_seconds_ms, k_time_three_seconds_ms, k_time_three_seconds_ms},
        {k_time_one_second_ms, k_time_one_second_ms, k_time_five_seconds_ms, k_time_five_seconds_ms, k_time_five_seconds_ms, k_time_five_seconds_ms}
    };

    for (const auto& deltasMs : scenarios) {
        FakeClock clock{};
        yaha::MessageTree tree = makeTree(clock,
                                          yaha::MessageTreeConfig::k_default_max_history_length,
                                          yaha::MessageTreeConfig::k_default_history_hysterese,
                                          yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                          3U);

        clock.nowMs = 0;
        tree.addData(yaha::Message{"sensor/no_dup_search", std::string{"steady"}});

        std::int64_t accumulatedMs = 0;
        for (const std::int64_t deltaMs : deltasMs) {
            accumulatedMs += deltaMs;
            clock.nowMs = accumulatedMs;
            tree.addData(yaha::Message{"sensor/no_dup_search", std::string{"steady"}});
        }

        const auto nodes = tree.getSection("sensor/no_dup_search", 0U, true, true);
        REQUIRE(nodes.size() == 1U);
        CAPTURE(deltasMs);
        REQUIRE(historyHasUniqueTimestamps(nodes.front().history));
    }
}

TEST_CASE("history_is_returned_newest_first", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    clock.nowMs = k_time_one_second_ms;
    tree.addData(yaha::Message{"sensor/order", 1.0});
    clock.nowMs = k_time_two_seconds_ms;
    tree.addData(yaha::Message{"sensor/order", k_value_two});
    clock.nowMs = k_time_three_seconds_ms;
    tree.addData(yaha::Message{"sensor/order", k_value_three});

    const auto nodes = tree.getSection("sensor/order", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(nodes.front().history[0].timeMs > nodes.front().history[1].timeMs);
}

TEST_CASE("history_grouping_compares_reason_messages_only", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(makeReasonedMessage("sensor/reason_group", 1.0, "same", "2026-01-01T00:00:00Z"));
    clock.nowMs += k_tick_ms;
    tree.addData(makeReasonedMessage("sensor/reason_group", k_value_two, "same", "2026-01-01T00:00:10Z"));
    clock.nowMs += k_tick_ms;
    tree.addData(makeReasonedMessage("sensor/reason_group", k_value_three, "same", "2026-01-01T00:00:20Z"));

    const auto nodes = tree.getSection("sensor/reason_group", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() == 2U);
    REQUIRE(nodes.front().history[1].reason.size() == 1U);
    REQUIRE(nodes.front().history[1].reason[0].timestamp == "2026-01-01T00:00:00Z");
}

TEST_CASE("history_single_compression_keeps_total_entry_count", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      1U,
                                      yaha::MessageTreeConfig::k_default_length_for_further_compression);

    std::size_t insertedMessages = 0U;
    const std::string topic{"sensor/count_single"};

    for (int step = 0; step < k_count_integrity_steps; ++step) {
        clock.nowMs = static_cast<std::int64_t>(step) * k_tick_ms;
        const double value = (step % 2 == 0) ? 1.0 : k_value_two;
        tree.addData(yaha::Message{topic, value});
        insertedMessages += 1U;
        requireTotalEntryCount(tree, topic, insertedMessages);
    }
}

TEST_CASE("history_time_value_compression_keeps_total_entry_count", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    std::size_t insertedMessages = 0U;
    const std::string topic{"sensor/count_time_value"};

    for (int step = 0; step < k_count_integrity_steps; ++step) {
        clock.nowMs = static_cast<std::int64_t>(step) * k_tick_ms;
        tree.addData(makeReasonedMessage(topic,
                                         static_cast<double>(step + 1),
                                         "source",
                                         "1970-01-01T00:00:00.000Z"));
        insertedMessages += 1U;
        requireTotalEntryCount(tree, topic, insertedMessages);
    }
}

TEST_CASE("history_time_compression_keeps_total_entry_count", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U,
                                      k_interval_upper_bound_factor,
                                      0U,
                                      k_interval_lower_bound_factor,
                                      0U);

    const std::vector<std::int64_t> timestamps{
        k_time_zero_ms,
        k_time_one_second_ms,
        k_time_four_seconds_ms,
        k_time_nine_seconds_ms,
        k_time_fifteen_seconds_ms,
        k_time_twenty_seconds_ms,
    };

    std::size_t insertedMessages = 0U;
    const std::string topic{"sensor/count_time"};
    for (const auto timestamp : timestamps) {
        clock.nowMs = timestamp;
        tree.addData(makeReasonedMessage(topic,
                                         std::string{"steady"},
                                         "origin",
                                         "1970-01-01T00:00:00.000Z"));
        insertedMessages += 1U;
        requireTotalEntryCount(tree, topic, insertedMessages);
    }
}

TEST_CASE("history_interval_compression_keeps_total_entry_count", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    std::size_t insertedMessages = 0U;
    const std::string topic{"sensor/count_interval"};
    for (int step = 0; step < k_count_integrity_steps; ++step) {
        clock.nowMs = static_cast<std::int64_t>(step) * k_tick_ms;
        tree.addData(makeReasonedMessage(topic,
                                         k_value_five,
                                         "source",
                                         "1970-01-01T00:00:00.000Z"));
        insertedMessages += 1U;
        requireTotalEntryCount(tree, topic, insertedMessages);
    }
}

TEST_CASE("history_time_to_interval_transition_keeps_total_entry_count", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      3U);

    const std::vector<std::int64_t> timestamps{
        k_time_zero_ms,
        k_time_one_second_ms,
        k_time_five_seconds_ms,
        k_time_nine_seconds_ms,
        k_time_thirteen_seconds_ms,
        k_time_fifteen_seconds_ms,
    };

    std::size_t insertedMessages = 0U;
    const std::string topic{"sensor/count_transition"};
    for (const auto timestamp : timestamps) {
        clock.nowMs = timestamp;
        tree.addData(makeReasonedMessage(topic,
                                         std::string{"steady"},
                                         "source",
                                         "1970-01-01T00:00:00.000Z"));
        insertedMessages += 1U;
        requireTotalEntryCount(tree, topic, insertedMessages);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("history_multi_reason_same_device_timestamp_adds_exactly_one_logical_entry_per_update", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    const std::string topic{"first/study/main/light/light on time"};
    const std::string deviceTimestamp{"2026-05-08T23:18:38.492Z"};
    const std::string brokerTimestamp{"2026-05-08T23:18:38.496Z"};

    std::size_t insertedMessages = 0U;
    for (int step = 0; step < k_count_integrity_steps; ++step) {
        clock.nowMs = static_cast<std::int64_t>(step) * k_time_ten_seconds_ms;

        yaha::Message message{topic, k_light_on_time_seconds};
        message.addReason("received from arduino", deviceTimestamp);
        message.addReason("received by broker", brokerTimestamp);

        tree.addData(message);
        insertedMessages += 1U;

        requireTotalEntryCount(tree, topic, insertedMessages);
    }

    const auto nodes = tree.getSection(topic, 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() >= 2U);

    const std::int64_t repeatedTimestamp = nodes.front().history.front().timeMs;
    const bool allHistoryTimestampsEqual = std::all_of(
        nodes.front().history.begin(),
        nodes.front().history.end(),
        [repeatedTimestamp](const yaha::MessageTreeHistoryEntry& entry) {
            return entry.timeMs == repeatedTimestamp;
        });
    REQUIRE(allHistoryTimestampsEqual);
}

TEST_CASE("history_stale_first_reason_timestamp_does_not_collapse_new_updates", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    const std::string topic{"first/study/main/light/light on time"};
    const std::string staleFirstReasonTimestamp{"2026-05-09T07:16:43+02:00"};
    const std::vector<std::string> brokerTimestamps{
        "2026-05-09T05:16:44.220Z",
        "2026-05-09T05:16:54.220Z",
        "2026-05-09T05:17:04.220Z",
    };

    for (std::size_t index = 0U; index < brokerTimestamps.size(); ++index) {
        clock.nowMs = static_cast<std::int64_t>(index) * k_time_ten_seconds_ms;

        yaha::Message message{topic, std::string{"off"}};
        message.addReason("received by broker", brokerTimestamps[index]);
        message.addReason("Request by browser", staleFirstReasonTimestamp);

        tree.addData(message);

        const auto nodes = tree.getSection(topic, 0U, true, true);
        REQUIRE(nodes.size() == 1U);

        std::unordered_set<std::int64_t> projectedTimes{};
        projectedTimes.insert(nodes.front().timeMs);
        for (const auto& historyEntry : nodes.front().history) {
            projectedTimes.insert(historyEntry.timeMs);
        }

        REQUIRE(projectedTimes.size() == (index + 1U));
    }
}

TEST_CASE("history_distinct_first_reason_timestamps_keep_distinct_projected_times", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock,
                                      yaha::MessageTreeConfig::k_default_max_history_length,
                                      yaha::MessageTreeConfig::k_default_history_hysterese,
                                      yaha::MessageTreeConfig::k_default_max_values_per_history_entry,
                                      k_length_for_time_value_only);

    const std::string topic{"first/study/main/light/light on time"};
    const std::vector<std::string> firstReasonTimestamps{
        "2026-05-09T05:16:44.220Z",
        "2026-05-09T05:16:54.220Z",
        "2026-05-09T05:17:04.220Z",
    };

    for (std::size_t index = 0U; index < firstReasonTimestamps.size(); ++index) {
        clock.nowMs = static_cast<std::int64_t>(index) * k_time_ten_seconds_ms;

        yaha::Message message{topic, std::string{"off"}};
        message.addReason("older metadata", "2026-05-09T05:16:40.000Z");
        message.addReason("received by broker", firstReasonTimestamps[index]);

        tree.addData(message);

        const auto nodes = tree.getSection(topic, 0U, true, true);
        REQUIRE(nodes.size() == 1U);

        std::unordered_set<std::int64_t> projectedTimes{};
        projectedTimes.insert(nodes.front().timeMs);
        for (const auto& historyEntry : nodes.front().history) {
            projectedTimes.insert(historyEntry.timeMs);
        }

        REQUIRE(projectedTimes.size() == (index + 1U));
    }
}

TEST_CASE("get_section_respects_depth", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"home/l1", std::string{"a"}});
    tree.addData(yaha::Message{"home/l1/l2", std::string{"b"}});
    tree.addData(yaha::Message{"home/l1/l2/l3", std::string{"c"}});

    const auto depth0 = tree.getSection("home/l1", 0U, false, true);
    const auto depth1 = tree.getSection("home/l1", 1U, false, true);

    REQUIRE(containsTopic(depth0, "home/l1"));
    REQUIRE_FALSE(containsTopic(depth0, "home/l1/l2"));

    REQUIRE(containsTopic(depth1, "home/l1"));
    REQUIRE(containsTopic(depth1, "home/l1/l2"));
    REQUIRE_FALSE(containsTopic(depth1, "home/l1/l2/l3"));
}

TEST_CASE("get_section_can_exclude_reason_and_history", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message first{"home/r1", std::string{"on"}};
    first.addReason("origin", "2026-01-01T00:00:00Z");
    tree.addData(first);

    clock.nowMs += k_tick_ms;
    yaha::Message second{"home/r1", std::string{"off"}};
    second.addReason("updated", "2026-01-01T00:00:01Z");
    tree.addData(second);

    const auto nodes = tree.getSection("home/r1", 0U, false, false);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().reason.empty());
    REQUIRE(nodes.front().history.empty());
}

TEST_CASE("get_section_excludes_node_reason_but_keeps_history_reasons", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    yaha::Message first{"home/r2", std::string{"on"}};
    first.addReason("origin", "2026-01-01T00:00:00Z");
    tree.addData(first);

    clock.nowMs += k_tick_ms;
    yaha::Message second{"home/r2", std::string{"off"}};
    second.addReason("updated", "2026-01-01T00:00:01Z");
    tree.addData(second);

    const auto nodes = tree.getSection("home/r2", 0U, true, false);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().reason.empty());
    REQUIRE(nodes.front().history.size() == 1U);
    REQUIRE(nodes.front().history[0].reason.size() == 1U);
    REQUIRE(nodes.front().history[0].reason[0].message == "origin");
}

TEST_CASE("get_nodes_returns_only_changed_or_new_nodes", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"home/a", std::string{"same"}});
    tree.addData(yaha::Message{"home/b", std::string{"new"}});

    std::vector<yaha::MessageTreeSnapshotNode> snapshot{};
    snapshot.push_back({"home/a", std::string{"same"}, {}, {}});

    const auto nodes = tree.getNodes(snapshot);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "home/b");
}

TEST_CASE("cleanup_removes_stale_nodes_and_prunes_empty_branches", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"old/topic", std::string{"x"}});

    constexpr std::int64_t millisPerDay = 86400000;
    clock.nowMs += 2 * millisPerDay;

    tree.addData(yaha::Message{"fresh/topic", std::string{"y"}});

    const std::size_t removed = tree.cleanup(1U);
    const auto remaining = tree.getSection("", 5U, false, true);

    REQUIRE(removed == 1U);
    REQUIRE_FALSE(containsTopic(remaining, "old/topic"));
    REQUIRE(containsTopic(remaining, "fresh/topic"));
}

TEST_CASE("wildcard_filter_matching_not_used_in_tree_queries", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"literal/+/hash/#", std::string{"v"}});

    const auto exact = tree.getSection("literal/+/hash/#", 0U, false, true);
    const auto wildcardPrefix = tree.getSection("literal", 5U, false, true);

    REQUIRE(exact.size() == 1U);
    REQUIRE(exact.front().topic == "literal/+/hash/#");
    REQUIRE(containsTopic(wildcardPrefix, "literal/+/hash/#"));
}
