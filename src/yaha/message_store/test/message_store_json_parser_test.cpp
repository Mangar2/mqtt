#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "yaha/message_store/message_store_json_parser.h"

TEST_CASE("message_store_json_parser_parses_snapshot_array_values", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};
    const bool parseSucceeded = yaha::message_store_json::parseSnapshotBody(
        "["
        "{\"topic\":\"home/light\",\"value\":\"on\\n\"},"
        "{\"topic\":\"home/temp\",\"value\":21.5}"
        "]",
        nodes);

    REQUIRE(parseSucceeded);
    REQUIRE(nodes.size() == 2U);
    REQUIRE(nodes[0].topic == "home/light");
    REQUIRE(std::holds_alternative<std::string>(nodes[0].value));
    REQUIRE(std::holds_alternative<double>(nodes[1].value));
}

TEST_CASE("message_store_json_parser_rejects_malformed_snapshot_payload", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};

    REQUIRE_FALSE(yaha::message_store_json::parseSnapshotBody("{\"topic\":\"x\"}", nodes));
    REQUIRE_FALSE(yaha::message_store_json::parseSnapshotBody("[{\"topic\":\"x\",\"value\":\"y\"", nodes));
    REQUIRE_FALSE(yaha::message_store_json::parseSnapshotBody("[{\"topic\":\"x\",\"value\":\"bad\\u\"}]", nodes));
}

TEST_CASE("message_store_json_parser_accepts_empty_snapshot_with_whitespace", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};
    const bool parseSucceeded = yaha::message_store_json::parseSnapshotBody("  [   ]  ", nodes);

    REQUIRE(parseSucceeded);
    REQUIRE(nodes.empty());
}

TEST_CASE("message_store_json_parser_rejects_snapshot_entries_missing_required_fields", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};
    REQUIRE_FALSE(yaha::message_store_json::parseSnapshotBody("[{}]", nodes));
    REQUIRE_FALSE(yaha::message_store_json::parseSnapshotBody("[{\"topic\":\"only-topic\"}]", nodes));
}

TEST_CASE("message_store_json_parser_skips_unknown_snapshot_fields", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};
    const bool parseSucceeded = yaha::message_store_json::parseSnapshotBody(
        "[{\"topic\":\"home/light\",\"value\":\"on\",\"meta\":{\"nested\":[1,2,{\"x\":true}]}}]",
        nodes);

    REQUIRE(parseSucceeded);
    REQUIRE(nodes.size() == 1U);
    REQUIRE(nodes[0].topic == "home/light");
}

TEST_CASE("message_store_json_parser_parses_optional_snapshot_time", "[message_store]") {
    std::vector<yaha::MessageTreeSnapshotNode> nodes{};
    const bool parseSucceeded = yaha::message_store_json::parseSnapshotBody(
        "["
        "{\"topic\":\"home/light\",\"value\":\"on\",\"time\":\"2024-03-21T10:15:30.123Z\"},"
        "{\"topic\":\"home/temp\",\"value\":21.5,\"time\":\"invalid\"}"
        "]",
        nodes);

    REQUIRE(parseSucceeded);
    REQUIRE(nodes.size() == 2U);
    REQUIRE(nodes[0].timeMs.has_value());
    REQUIRE_FALSE(nodes[1].timeMs.has_value());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("message_store_json_parser_parses_sensor_post_with_nodes_and_flags", "[message_store]") {
    yaha::message_store_json::SensorPostRequest request{};
    const bool parseSucceeded = yaha::message_store_json::parseSensorPostBody(
        "{"
        "\"topic\":\"/house\","
        "\"history\":\"yes\","
        "\"reason\":true,"
        "\"levelAmount\":\"3\","
        "\"nodes\":[{\"topic\":\"house/light\",\"value\":\"on\"}]"
        "}",
        request);

    REQUIRE(parseSucceeded);
    REQUIRE(request.topicPrefix == "house");
    REQUIRE(request.includeHistory);
    REQUIRE(request.includeReason);
    REQUIRE(request.levelAmount == 3U);
    REQUIRE(request.hasNodes);
    REQUIRE(request.nodesJson.find("house/light") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("message_store_json_parser_applies_defaults_for_invalid_sensor_values", "[message_store]") {
    yaha::message_store_json::SensorPostRequest request{};
    const bool parseSucceeded = yaha::message_store_json::parseSensorPostBody(
        "{"
        "\"topic\":\"home\","
        "\"history\":\"invalid\","
        "\"reason\":\"invalid\","
        "\"levelAmount\":\"abc\","
        "\"nodes\":null"
        "}",
        request);

    REQUIRE(parseSucceeded);
    REQUIRE(request.topicPrefix == "home");
    REQUIRE_FALSE(request.includeHistory);
    REQUIRE_FALSE(request.includeReason);
    REQUIRE(request.levelAmount == 1U);
    REQUIRE_FALSE(request.hasNodes);
    REQUIRE(request.nodesJson.empty());
}

TEST_CASE("message_store_json_parser_skips_unknown_nested_sensor_fields", "[message_store]") {
    yaha::message_store_json::SensorPostRequest request{};
    const bool parseSucceeded = yaha::message_store_json::parseSensorPostBody(
        "{"
        "\"topic\":\"home\","
        "\"history\":\" yes \","
        "\"reason\":\" on \","
        "\"levelAmount\":\" 4 \","
        "\"unknownObject\":{\"a\":[1,2,{\"b\":\"x\"}]},"
        "\"unknownArray\":[\"x\",{\"y\":false}],"
        "\"nodes\":[]"
        "}",
        request);

    REQUIRE(parseSucceeded);
    REQUIRE(request.topicPrefix == "home");
    REQUIRE(request.includeHistory);
    REQUIRE(request.includeReason);
    REQUIRE(request.levelAmount == 4U);
    REQUIRE_FALSE(request.hasNodes);
}

TEST_CASE("message_store_json_parser_rejects_invalid_nested_sensor_json", "[message_store]") {
    yaha::message_store_json::SensorPostRequest request{};
    REQUIRE_FALSE(yaha::message_store_json::parseSensorPostBody(
        "{\"topic\":\"home\",\"unknownObject\":{\"a\":[1,2}\"}",
        request));
}

TEST_CASE("message_store_json_parser_parses_escaped_topic_sequences", "[message_store]") {
    yaha::message_store_json::SensorPostRequest request{};
    const bool parseSucceeded = yaha::message_store_json::parseSensorPostBody(
        "{\"topic\":\"home\\r\\troom\\/light\"}",
        request);

    REQUIRE(parseSucceeded);
    REQUIRE(request.topicPrefix.find("home") == 0U);
}
