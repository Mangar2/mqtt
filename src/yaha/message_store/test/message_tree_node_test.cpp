#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/message_store/message_tree_node.h"

namespace {

constexpr std::int64_t k_history_time_first_ms{1000};
constexpr std::int64_t k_history_time_second_ms{2000};
constexpr std::int64_t k_history_time_third_ms{3000};

TEST_CASE("message_tree_node_reason_roundtrip_uses_plain_reason_list", "[message_store]") {
    yaha::MessageTreeNode node{};

    yaha::ReasonList reason{};
    reason.push_back(yaha::ReasonEntry{.message = "sensor", .timestamp = "2026-01-01T00:00:00.000Z"});
    reason.push_back(yaha::ReasonEntry{.message = "broker", .timestamp = "2026-01-01T00:00:01Z"});

    node.setReasonList(reason);

    const yaha::ReasonList roundtrip = node.reason();
    REQUIRE(roundtrip.size() == 2U);
    REQUIRE(roundtrip[0].message == "sensor");
    REQUIRE(roundtrip[0].timestamp == "2026-01-01T00:00:00.000Z");
    REQUIRE(roundtrip[1].message == "broker");
    REQUIRE(roundtrip[1].timestamp == "2026-01-01T00:00:01Z");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("message_tree_node_history_add_remove_clear_and_roundtrip", "[message_store]") {
    yaha::MessageTreeNode node{};

    yaha::ReasonList firstReason{};
    firstReason.push_back(yaha::ReasonEntry{.message = "first", .timestamp = "2026-01-01T00:00:00Z"});
    node.addHistoryEntry(k_history_time_first_ms, std::string{"v1"}, firstReason);

    yaha::ReasonList secondReason{};
    secondReason.push_back(yaha::ReasonEntry{.message = "second", .timestamp = "2026-01-01T00:00:01.100Z"});
    node.addHistoryEntry(k_history_time_second_ms, std::string{"v2"}, secondReason);

    REQUIRE(node.history().size() == 2U);
    REQUIRE(std::get<std::string>(node.history()[0].value) == "v1");
    REQUIRE(node.history()[0].reason().front().message == "first");
    REQUIRE(std::get<std::string>(node.history()[1].value) == "v2");
    REQUIRE(node.history()[1].reason().front().message == "second");

    REQUIRE(node.removeHistoryEntry(0U));
    REQUIRE_FALSE(node.removeHistoryEntry(99U));
    REQUIRE(node.history().size() == 1U);
    REQUIRE(node.history()[0].reason().front().message == "second");

    node.clearHistory();
    REQUIRE(node.history().empty());
}

TEST_CASE("message_tree_node_set_history_entries_copies_foreign_entries", "[message_store]") {
    yaha::MessageTreeNode sourceNode{};
    yaha::ReasonList sourceReason{};
    sourceReason.push_back(yaha::ReasonEntry{.message = "foreign", .timestamp = "2026-01-01T00:00:02Z"});
    sourceNode.addHistoryEntry(k_history_time_third_ms, std::string{"v3"}, sourceReason);

    yaha::MessageTreeNode targetNode{};
    targetNode.setHistoryEntries({sourceNode.history().front()});

    REQUIRE(targetNode.history().size() == 1U);
    REQUIRE(std::get<std::string>(targetNode.history().front().value) == "v3");
    REQUIRE(targetNode.history().front().reason().size() == 1U);
    REQUIRE(targetNode.history().front().reason().front().message == "foreign");
    REQUIRE(targetNode.history().front().reason().front().timestamp == "2026-01-01T00:00:02Z");
}

} // namespace
