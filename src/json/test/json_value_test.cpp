#include <catch2/catch_test_macros.hpp>

#include "json/json_error.h"
#include "json/json_value.h"

#include <optional>
#include <string>

using mqtt::json::JsonError;
using mqtt::json::JsonException;
using mqtt::json::JsonValue;

namespace {

constexpr double k_expected_list_value{3.0};
constexpr double k_expected_array_value{42.0};

} // namespace

TEST_CASE("parse_object_fields_are_accessible", "[json]") {
    const std::string inputText{R"({"name":"yaha","active":true,"list":[1,2,3],"text":"line\nnext"})"};

    const JsonValue parsedValue = JsonValue::parse(inputText);

    REQUIRE(parsedValue.is_object());
    CHECK(parsedValue.at("name").as_string() == "yaha");
    CHECK(parsedValue.at("active").as_boolean());
    CHECK(parsedValue.at("list").at(2U).as_number() == k_expected_list_value);
    CHECK(parsedValue.at("text").as_string() == "line\nnext");
}

TEST_CASE("stringify_and_parse_roundtrip_keeps_values", "[json]") {
    const JsonValue parsedValue = JsonValue::parse(
        R"({"name":"yaha","active":true,"list":[1,2,3],"text":"line\nnext"})");

    const std::string serializedText = parsedValue.stringify();
    const JsonValue reparsedValue = JsonValue::parse(serializedText);

    CHECK(reparsedValue.at("name").as_string() == "yaha");
    CHECK(reparsedValue.at("list").at(0U).as_number() == 1.0);
}

TEST_CASE("parse_invalid_json_returns_nullopt", "[json]") {
    const std::optional<JsonValue> parsedValue = JsonValue::try_parse("{ \"a\": [1, 2 }");
    REQUIRE_FALSE(parsedValue.has_value());
}

TEST_CASE("parse_invalid_json_throws_with_offset", "[json]") {
    try {
        (void)JsonValue::parse("{ \"a\" 1 }");
        FAIL("expected JsonException");
    } catch (const JsonException& exception) {
        CHECK(exception.error() == JsonError::UnexpectedToken);
        CHECK(exception.offset() > 0U);
    }
}

TEST_CASE("object_operator_brackets_auto_create", "[json]") {
    JsonValue rootValue{};
    rootValue["rules"]["entry"]["enabled"] = JsonValue{true};
    rootValue["rules"]["entry"]["name"] = JsonValue{"main"};

    REQUIRE(rootValue.is_object());
    CHECK(rootValue.at("rules").at("entry").at("enabled").as_boolean());
    CHECK(rootValue.at("rules").at("entry").at("name").as_string() == "main");
}

TEST_CASE("array_operator_brackets_auto_growth", "[json]") {
    JsonValue arrayValue{};
    arrayValue[2U] = JsonValue{k_expected_array_value};

    REQUIRE(arrayValue.is_array());
    CHECK(arrayValue.size() == 3U);
    CHECK(arrayValue.at(0U).is_null());
    CHECK(arrayValue.at(1U).is_null());
    CHECK(arrayValue.at(2U).as_number() == k_expected_array_value);
}

TEST_CASE("push_back_on_null_creates_array", "[json]") {
    JsonValue arrayValue{};
    arrayValue.push_back(JsonValue{"a"});
    arrayValue.push_back(JsonValue{"b"});

    REQUIRE(arrayValue.is_array());
    CHECK(arrayValue.size() == 2U);
    CHECK(arrayValue.at(0U).as_string() == "a");
    CHECK(arrayValue.at(1U).as_string() == "b");
}

TEST_CASE("invalid_type_access_throws", "[json]") {
    const JsonValue numberValue{5.0};
    try {
        (void)numberValue.as_string();
        FAIL("expected JsonException");
    } catch (const JsonException& exception) {
        CHECK(exception.error() == JsonError::InvalidType);
    }
}

TEST_CASE("missing_key_throws", "[json]") {
    JsonValue objectValue = JsonValue::object();
    objectValue["present"] = JsonValue{1.0};

    try {
        (void)objectValue.at("missing");
        FAIL("expected JsonException");
    } catch (const JsonException& exception) {
        CHECK(exception.error() == JsonError::MissingKey);
    }
}

TEST_CASE("out_of_range_index_throws", "[json]") {
    JsonValue arrayValue = JsonValue::array();
    arrayValue.push_back(JsonValue{1.0});

    try {
        (void)arrayValue.at(3U);
        FAIL("expected JsonException");
    } catch (const JsonException& exception) {
        CHECK(exception.error() == JsonError::IndexOutOfRange);
    }
}

TEST_CASE("unicode_escape_parsing_supports_surrogate_pair", "[json]") {
    const JsonValue parsedValue = JsonValue::parse(R"("\uD83D\uDE03")");

    REQUIRE(parsedValue.is_string());
    CHECK_FALSE(parsedValue.as_string().empty());
}
