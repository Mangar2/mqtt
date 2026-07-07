#include <catch2/catch_test_macros.hpp>

#include "helper/string_helper.h"

#include <string>
#include <vector>

using mqtt::helper::split;
using mqtt::helper::toLower;
using mqtt::helper::trim;

TEST_CASE("tolower_mixed_case_returns_lowercase", "[helper]") {
    CHECK(toLower("HeLLo World_42") == "hello world_42");
}

TEST_CASE("tolower_empty_string_returns_empty", "[helper]") {
    CHECK(toLower("").empty());
}

TEST_CASE("tolower_already_lowercase_is_unchanged", "[helper]") {
    CHECK(toLower("already lower") == "already lower");
}

TEST_CASE("trim_removes_leading_and_trailing_whitespace", "[helper]") {
    CHECK(trim(" \t\n hello world \r\n") == "hello world");
}

TEST_CASE("trim_all_whitespace_returns_empty", "[helper]") {
    CHECK(trim("   \t  ").empty());
}

TEST_CASE("trim_no_whitespace_is_unchanged", "[helper]") {
    CHECK(trim("clean") == "clean");
}

TEST_CASE("trim_empty_string_returns_empty", "[helper]") {
    CHECK(trim("").empty());
}

TEST_CASE("split_basic_delimiter_trims_each_token", "[helper]") {
    const std::vector<std::string> expectedTokens{"a", "b", "c"};
    CHECK(split("a, b ,c", ',') == expectedTokens);
}

TEST_CASE("split_consecutive_delimiters_yield_empty_token", "[helper]") {
    const std::vector<std::string> expectedTokens{"a", "", "b"};
    CHECK(split("a,,b", ',') == expectedTokens);
}

TEST_CASE("split_trailing_delimiter_has_no_trailing_empty_token", "[helper]") {
    const std::vector<std::string> expectedTokens{"a", "b"};
    CHECK(split("a,b,", ',') == expectedTokens);
}

TEST_CASE("split_empty_string_returns_empty_vector", "[helper]") {
    CHECK(split("", ',').empty());
}

TEST_CASE("split_no_delimiter_present_returns_single_token", "[helper]") {
    const std::vector<std::string> expectedTokens{"single"};
    CHECK(split("single", ',') == expectedTokens);
}
