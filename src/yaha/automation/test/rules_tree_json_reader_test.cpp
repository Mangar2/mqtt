#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "yaha/automation/rules_tree_json_reader.h"

TEST_CASE("rules_tree_json_reader_parses_valid_object_and_array", "[yaha][automation]") {
    const std::string jsonText =
        "{\"rules\":{\"demo\":{\"topic\":\"house/light/set\",\"value\":\"on\"}},\"flags\":[true,false,null],\"num\":12.5}";

    const yaha::RuleTreeJsonReadResult result = yaha::RulesTreeJsonReader::parseJsonText(jsonText);

    REQUIRE(result.success);
    REQUIRE(result.errors.empty());
    REQUIRE(result.root.isObject());
    REQUIRE(result.root.asObject().contains("rules"));
    REQUIRE(result.root.asObject().contains("flags"));
}

TEST_CASE("rules_tree_json_reader_reports_trailing_character_error", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult result = yaha::RulesTreeJsonReader::parseJsonText("{}x");

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("rules_tree_json_reader_reports_invalid_escape_and_unicode_escape", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult invalidEscape =
        yaha::RulesTreeJsonReader::parseJsonText("{\"x\":\"a\\q\"}");
    REQUIRE_FALSE(invalidEscape.success);
    REQUIRE_FALSE(invalidEscape.errors.empty());

    const yaha::RuleTreeJsonReadResult unicodeEscape =
        yaha::RulesTreeJsonReader::parseJsonText("{\"x\":\"\\u0041\"}");
    REQUIRE_FALSE(unicodeEscape.success);
    REQUIRE_FALSE(unicodeEscape.errors.empty());
}

TEST_CASE("rules_tree_json_reader_parse_file_reports_open_error", "[yaha][automation]") {
    const auto missingPath =
        (std::filesystem::temp_directory_path() / "yaha_rules_tree_missing_test_file.json").string();

    const yaha::RuleTreeJsonReadResult result = yaha::RulesTreeJsonReader::parseJsonFile(missingPath);

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
    REQUIRE(result.errors.front().line == 0U);
    REQUIRE(result.errors.front().column == 0U);
}

TEST_CASE("rules_tree_json_reader_parses_negative_fractional_exponent_number", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult result = yaha::RulesTreeJsonReader::parseJsonText("{\"n\":-1.2e+3}");

    REQUIRE(result.success);
    REQUIRE(result.root.isObject());
    REQUIRE(result.root.asObject().contains("n"));
}

TEST_CASE("rules_tree_json_reader_reports_invalid_number_forms", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult invalidFraction = yaha::RulesTreeJsonReader::parseJsonText("{\"n\":1.}");
    REQUIRE_FALSE(invalidFraction.success);
    REQUIRE_FALSE(invalidFraction.errors.empty());

    const yaha::RuleTreeJsonReadResult invalidExponent = yaha::RulesTreeJsonReader::parseJsonText("{\"n\":1e}");
    REQUIRE_FALSE(invalidExponent.success);
    REQUIRE_FALSE(invalidExponent.errors.empty());
}

TEST_CASE("rules_tree_json_reader_reports_invalid_keyword_and_unterminated_string", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult invalidKeyword = yaha::RulesTreeJsonReader::parseJsonText("{\"x\":tru}");
    REQUIRE_FALSE(invalidKeyword.success);
    REQUIRE_FALSE(invalidKeyword.errors.empty());

    const yaha::RuleTreeJsonReadResult unterminatedString = yaha::RulesTreeJsonReader::parseJsonText("{\"x\":\"abc}");
    REQUIRE_FALSE(unterminatedString.success);
    REQUIRE_FALSE(unterminatedString.errors.empty());
}
