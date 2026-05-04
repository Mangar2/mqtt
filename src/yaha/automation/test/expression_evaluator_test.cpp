#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/automation/expression_parser.h"

namespace {

[[nodiscard]] yaha::FieldScriptAst parseScript(const std::string& script) {
    const yaha::ExpressionParseResult parsed = yaha::ExpressionParser::parse(script);
    REQUIRE(parsed.success);
    return parsed.ast;
}

} // namespace

TEST_CASE("expression_evaluator_evaluates_map_declaration_and_call", "[yaha][automation]") {
    const auto ast = parseScript(
        "presence = (1: awake, on: awake, default: absent)\n"
        "presence($SYS/presence/set)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence/set", std::string{"on"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(result.errors.empty());
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "awake");
    REQUIRE(result.usedVariables.contains("$SYS/presence/set"));
}

TEST_CASE("expression_evaluator_evaluates_if_expression", "[yaha][automation]") {
    const auto ast = parseScript("if($SYS/presence = awake, on, off)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "on");
}

TEST_CASE("expression_evaluator_supports_time_arithmetic_in_minutes", "[yaha][automation]") {
    const auto ast = parseScript("\"/time\" >= \"/sunrise\" - 10");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    vars.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{6} + std::chrono::minutes{55}}});
    vars.insert({"/sunrise", std::chrono::system_clock::time_point{day + std::chrono::hours{7} + std::chrono::minutes{0}}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<bool>(result.value));
    REQUIRE(std::get<bool>(result.value));
}

TEST_CASE("expression_evaluator_reports_undefined_variable", "[yaha][automation]") {
    const auto ast = parseScript("$SYS/presence = awake");

    const yaha::ExpressionEvaluator::VariableMap vars;
    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE_FALSE(result.success);
    REQUIRE(result.errors.empty() == false);
}

TEST_CASE("expression_evaluator_can_evaluate_program_from_rules_fixture", "[yaha][automation]") {
    const auto ast = parseScript("if(\"/time\" < \"/sunrise\" + -15 and \"/time\" > \"7:00\" and \"/time\" < \"8:00\", on, off)");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    vars.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{7} + std::chrono::minutes{30}}});
    vars.insert({"/sunrise", std::chrono::system_clock::time_point{day + std::chrono::hours{8} + std::chrono::minutes{0}}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "on");
}
