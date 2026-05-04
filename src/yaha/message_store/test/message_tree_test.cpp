#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "yaha/message/message.h"
#include "yaha/message_store/message_tree.h"

namespace {

constexpr std::int64_t k_initial_now_ms{1000};
constexpr std::int64_t k_tick_ms{1000};
constexpr double k_temperature_before{21.0};
constexpr double k_temperature_after{22.0};
constexpr int k_history_last_step{6};

struct FakeClock {
    std::int64_t nowMs{k_initial_now_ms};
};

yaha::MessageTree makeTree(FakeClock& clock,
                           std::uint32_t maxHistoryLength = yaha::MessageTreeConfig::k_default_max_history_length,
                           std::uint32_t historyHysterese = yaha::MessageTreeConfig::k_default_history_hysterese,
                           std::uint32_t maxValuesPerHistoryEntry = yaha::MessageTreeConfig::k_default_max_values_per_history_entry) {
    yaha::MessageTreeConfig config{};
    config.maxHistoryLength = maxHistoryLength;
    config.historyHysterese = historyHysterese;
    config.maxValuesPerHistoryEntry = maxValuesPerHistoryEntry;
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

TEST_CASE("history_is_trimmed_with_hysteresis", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock, 3U, 1U);

    tree.addData(yaha::Message{"sensor/value", 1.0});
    for (int step = 2; step <= k_history_last_step; ++step) {
        clock.nowMs += k_tick_ms;
        tree.addData(yaha::Message{"sensor/value", static_cast<double>(step)});
    }

    const auto nodes = tree.getSection("sensor/value", 0U, true, true);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().history.size() <= 3U);
    REQUIRE_FALSE(nodes.front().history.empty());
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
    REQUIRE(nodes.front().history[0].timeMs == nodes.front().history[1].timeMs);
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

TEST_CASE("get_nodes_returns_only_changed_or_new_nodes", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"home/a", std::string{"same"}});
    tree.addData(yaha::Message{"home/b", std::string{"new"}});

    std::vector<yaha::MessageTreeSnapshotNode> snapshot{};
    snapshot.push_back({"home/a", std::string{"same"}, {}});

    const auto nodes = tree.getNodes(snapshot);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "home/b");
}

TEST_CASE("cleanup_removes_stale_nodes_and_prunes_empty_branches", "[message_store]") {
    FakeClock clock{};
    yaha::MessageTree tree = makeTree(clock);

    tree.addData(yaha::Message{"old/topic", std::string{"x"}});

    constexpr std::int64_t millis_per_day = 86400000;
    clock.nowMs += 2 * millis_per_day;

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
