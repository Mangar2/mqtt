#include <catch2/catch_test_macros.hpp>

#include "yaha/message/message_log_filter.h"

TEST_CASE("Message log filter matches exact topic", "[message][message_log_filter]") {
    REQUIRE(yaha::matchesTopicFilter("a/b/c", "a/b/c"));
}

TEST_CASE("Message log filter supports plus wildcard", "[message][message_log_filter]") {
    REQUIRE(yaha::matchesTopicFilter("a/b/c", "a/+/c"));
}

TEST_CASE("Message log filter supports hash wildcard tail", "[message][message_log_filter]") {
    REQUIRE(yaha::matchesTopicFilter("a/b/c/d", "a/#"));
}

TEST_CASE("Message log filter rejects non matching topic", "[message][message_log_filter]") {
    REQUIRE_FALSE(yaha::matchesTopicFilter("a/b/c", "a/+/d"));
}
