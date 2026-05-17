#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string>

#include "yaha/automation/rules_tree_json_reader.h"
#include "yaha/automation/rules_tree_parser.h"

namespace {

[[nodiscard]] std::string resolveRulesJsonPath() {
    const std::array<std::filesystem::path, 3U> candidatePaths = {
        std::filesystem::path{"test/rules.json"},
        std::filesystem::path{"../test/rules.json"},
        std::filesystem::path{"../../test/rules.json"}};

    for (const auto& candidatePath : candidatePaths) {
        if (std::filesystem::exists(candidatePath)) {
            return candidatePath.string();
        }
    }

    return "test/rules.json";
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("rules_tree_parser_parses_rules_json_without_errors", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult readResult = yaha::RulesTreeJsonReader::parseJsonFile(resolveRulesJsonPath());

    REQUIRE(readResult.success);
    REQUIRE(readResult.errors.empty());

    const yaha::RulesTreeParseResult parseResult = yaha::RulesTreeParser::parse(readResult.root);

    REQUIRE(parseResult.success());
    REQUIRE(parseResult.errors.empty());
    REQUIRE(parseResult.snippetsByPath.empty() == false);
}

TEST_CASE("rules_tree_parser_collects_external_topics_from_rules_json", "[yaha][automation]") {
    const yaha::RuleTreeJsonReadResult readResult = yaha::RulesTreeJsonReader::parseJsonFile(resolveRulesJsonPath());
    REQUIRE(readResult.success);

    const yaha::RulesTreeParseResult parseResult = yaha::RulesTreeParser::parse(readResult.root);
    REQUIRE(parseResult.success());

    REQUIRE(parseResult.externalVariables.empty() == false);
    REQUIRE(parseResult.externalVariables.contains("status/presence"));
    REQUIRE(parseResult.externalVariables.contains("status/presence/set"));
    REQUIRE(parseResult.externalVariables.contains("outdoor/garden/main/weather/temperature"));
    REQUIRE(parseResult.externalVariables.contains("first/hallway/main/temperature and humidity sensor/temperature in celsius"));
}
// NOLINTEND(readability-function-cognitive-complexity)
