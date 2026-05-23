#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "yaha/message/message.h"
#include "yaha/message_store/message_tree.h"

namespace {

constexpr std::int64_t k_now_ms{1000};

yaha::MessageTree makeTree() {
    yaha::MessageTreeConfig config{};
    config.nowMillisecondsProvider = []() {
        return k_now_ms;
    };
    return yaha::MessageTree{config};
}

} // namespace

TEST_CASE("message_tree_constructor_rejects_zero_max_history_length", "[message_store]") {
    yaha::MessageTreeConfig config{};
    config.maxHistoryLength = 0U;

    REQUIRE_THROWS_AS(yaha::MessageTree{config}, std::invalid_argument);
}

TEST_CASE("message_tree_constructor_rejects_hysterese_larger_than_max", "[message_store]") {
    yaha::MessageTreeConfig config{};
    config.maxHistoryLength = 3U;
    config.historyHysterese = 4U;

    REQUIRE_THROWS_AS(yaha::MessageTree{config}, std::invalid_argument);
}

TEST_CASE("message_tree_get_nodes_clears_reason_when_include_reason_false", "[message_store]") {
    yaha::MessageTree tree = makeTree();

    yaha::Message message{"home/light", std::string{"on"}};
    message.addReason("request", "2026-01-01T00:00:00Z");
    tree.addData(message);

    yaha::MessageTreeSnapshotNode snapshot{};
    snapshot.topic = "home/light";
    snapshot.value = std::string{"off"};

    const std::vector<yaha::MessageTreeSnapshotNode> snapshots{snapshot};
    const std::vector<yaha::MessageTreeNode> nodes = tree.getNodes(snapshots, false, false);

    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes.front().topic == "home/light");
    REQUIRE(nodes.front().reason.empty());
}

TEST_CASE("message_tree_read_compressed_rejects_unknown_value_token", "[message_store]") {
    yaha::MessageTree tree = makeTree();

    std::istringstream stream{
        "\"\"\n"
        "0\n"
        "1\n"
        "1000\n"
        "X\n"};

    REQUIRE_FALSE(tree.readCompressed(stream));
}

TEST_CASE("message_tree_read_compressed_rejects_unknown_history_entry_type", "[message_store]") {
    yaha::MessageTree tree = makeTree();

    std::istringstream stream{
        "\"\"\n"
        "0\n"
        "1\n"
        "1000\n"
        "N 1\n"
        "0\n"
        "1\n"
        "mystery\n"};

    REQUIRE_FALSE(tree.readCompressed(stream));
}
