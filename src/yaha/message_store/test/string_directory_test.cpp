#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "yaha/message_store/string_directory.h"

TEST_CASE("string_directory_add_returns_existing_index_for_duplicate", "[message_store]") {
    yaha::StringDirectory directory{};

    const std::uint16_t firstIndex = directory.add("alpha");
    const std::uint16_t duplicateIndex = directory.add("alpha");

    REQUIRE(firstIndex == duplicateIndex);
    REQUIRE(directory.size() == 1U);
    REQUIRE(directory.capacity() == 1U);
}

TEST_CASE("string_directory_reuses_free_slot_after_remove", "[message_store]") {
    yaha::StringDirectory directory{};

    const std::uint16_t alphaIndex = directory.add("alpha");
    const std::uint16_t betaIndex = directory.add("beta");
    REQUIRE(betaIndex == 1U);

    REQUIRE(directory.remove(alphaIndex));

    const std::uint16_t gammaIndex = directory.add("gamma");
    REQUIRE(gammaIndex == alphaIndex);
    REQUIRE(directory.size() == 2U);
    REQUIRE(directory.capacity() == 2U);
}

TEST_CASE("string_directory_get_returns_nullopt_for_empty_or_out_of_range_slots", "[message_store]") {
    yaha::StringDirectory directory{};

    const std::uint16_t alphaIndex = directory.add("alpha");
    REQUIRE(directory.remove(alphaIndex));

    REQUIRE_FALSE(directory.get(alphaIndex).has_value());
    REQUIRE_FALSE(directory.get(42U).has_value());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("string_directory_remove_updates_size_and_keeps_capacity", "[message_store]") {
    yaha::StringDirectory directory{};

    const std::uint16_t alphaIndex = directory.add("alpha");
    const auto betaIndex = directory.add("beta");

    REQUIRE(directory.size() == 2U);
    REQUIRE(directory.capacity() == 2U);
    REQUIRE(betaIndex == 1U);

    REQUIRE(directory.remove(alphaIndex));
    REQUIRE_FALSE(directory.remove(alphaIndex));
    REQUIRE(directory.size() == 1U);
    REQUIRE(directory.capacity() == 2U);
}

TEST_CASE("string_directory_add_rejects_empty_string", "[message_store]") {
    yaha::StringDirectory directory{};

    REQUIRE_THROWS_AS(directory.add(std::string{}), std::invalid_argument);
}