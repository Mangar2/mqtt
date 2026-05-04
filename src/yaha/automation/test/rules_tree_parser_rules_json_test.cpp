#include <catch2/catch_test_macros.hpp>

#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation/rules_tree_parser.h"

TEST_CASE("rules_tree_parser_parses_rules_json_without_errors", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult readResult = yaha::RulesTreeJsonReader::parseJsonFile("test/rules.json");

    REQUIRE(readResult.success);
    REQUIRE(readResult.errors.empty());

    const yaha::RulesTreeParseResult parseResult = yaha::RulesTreeParser::parse(readResult.root);

    REQUIRE(parseResult.success());
    REQUIRE(parseResult.errors.empty());
    REQUIRE(parseResult.snippetsByPath.empty() == false);
}

TEST_CASE("rules_tree_parser_collects_external_topics_from_rules_json", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult readResult = yaha::RulesTreeJsonReader::parseJsonFile("test/rules.json");
    REQUIRE(readResult.success);

    const yaha::RulesTreeParseResult parseResult = yaha::RulesTreeParser::parse(readResult.root);
    REQUIRE(parseResult.success());

    REQUIRE(parseResult.externalVariables.empty() == false);
    REQUIRE(parseResult.externalVariables.contains("$SYS/presence"));
    REQUIRE(parseResult.externalVariables.contains("$SYS/presence/set"));
    REQUIRE(parseResult.externalVariables.contains("outdoor/garden/main/weather/temperature"));
    REQUIRE(parseResult.externalVariables.contains("first/hallway/main/temperature and humidity sensor/temperature in celsius"));
}
