#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <chrono>
#include <ctime>
#include <string>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/automation/expression_parser.h"

namespace {

constexpr int k_time_hour_six{6};
constexpr int k_time_hour_seven{7};
constexpr int k_time_hour_eight{8};
constexpr int k_time_hour_nine{9};
constexpr int k_minutes_fifty_five{55};
constexpr int k_minutes_thirty{30};
constexpr double k_numeric_one{1.0};
constexpr double k_numeric_three{3.0};
constexpr std::size_t k_time_text_buffer_size{9U};

[[nodiscard]] yaha::FieldScriptAst parseScript(const std::string& script) {
    const yaha::ExpressionParseResult parsed = yaha::ExpressionParser::parse(script);
    REQUIRE(parsed.success);
    return parsed.ast;
}

[[nodiscard]] std::string localTimeText(const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    const auto conversionResult = localtime_s(&localCalendarTime, &epochSeconds);
    REQUIRE(conversionResult == 0);
#else
    const auto* conversionResult = localtime_r(&epochSeconds, &localCalendarTime);
    REQUIRE(conversionResult != nullptr);
#endif

    std::array<char, k_time_text_buffer_size> outputBuffer{};
    const auto charsWritten = std::strftime(outputBuffer.data(), outputBuffer.size(), "%H:%M:%S", &localCalendarTime);
    REQUIRE(charsWritten > 0U);
    return std::string{outputBuffer.data()};
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
    const auto ast = parseScript(R"("/time" >= "/sunrise" - 10)");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    vars.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_six} + std::chrono::minutes{k_minutes_fifty_five}}});
    vars.insert({"/sunrise", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_seven} + std::chrono::minutes{0}}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<bool>(result.value));
    REQUIRE(std::get<bool>(result.value));
}

TEST_CASE("expression_evaluator_uses_local_time_for_timepoint_comparisons_and_reason", "[yaha][automation]") {
    const auto ast = parseScript(R"("/time" >= "00:00")");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto nowTimePoint = std::chrono::system_clock::now();
    vars.insert({"/time", nowTimePoint});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<bool>(result.value));
    REQUIRE(std::get<bool>(result.value));

    const std::string expectedLocalClockText = localTimeText(nowTimePoint);
    REQUIRE(result.reason.find("/time (" + expectedLocalClockText + ")") != std::string::npos);
}

TEST_CASE("expression_evaluator_handles_undefined_variable_like_legacy", "[yaha][automation]") {
    const auto ast = parseScript("$SYS/presence = awake");

    const yaha::ExpressionEvaluator::VariableMap vars;
    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(result.errors.empty());
    REQUIRE(std::holds_alternative<bool>(result.value));
    REQUIRE_FALSE(std::get<bool>(result.value));
    REQUIRE(result.reason.find("false, undefined variables: $SYS/presence") != std::string::npos);
}

TEST_CASE("expression_evaluator_can_evaluate_program_from_rules_fixture", "[yaha][automation]") {
    const auto ast = parseScript(R"(if("/time" < "/sunrise" + -15 and "/time" > "7:00" and "/time" < "8:00", on, off))");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    vars.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_seven} + std::chrono::minutes{k_minutes_thirty}}});
    vars.insert({"/sunrise", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_eight} + std::chrono::minutes{0}}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "on");
}

TEST_CASE("expression_evaluator_map_call_uses_default_entry", "[yaha][automation]") {
    const auto ast = parseScript("state = (on: awake, default: absent)\nstate($SYS/presence/set)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence/set", std::string{"unknown"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "absent");
}

TEST_CASE("expression_evaluator_reports_map_call_without_default", "[yaha][automation]") {
    const auto ast = parseScript("state = (on: awake)\nstate($SYS/presence/set)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence/set", std::string{"unknown"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("expression_evaluator_reports_undefined_map_declaration", "[yaha][automation]") {
    const auto ast = parseScript("missing($SYS/presence)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("expression_evaluator_rejects_map_as_final_result", "[yaha][automation]") {
    const auto ast = parseScript("(default: on)");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("expression_evaluator_reports_invalid_arithmetic_operands", "[yaha][automation]") {
    const auto ast = parseScript("on + off");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("expression_evaluator_supports_numeric_arithmetic_result", "[yaha][automation]") {
    const auto ast = parseScript("1 + 2");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<double>(result.value));
    REQUIRE(std::get<double>(result.value) == Catch::Approx(k_numeric_three));
}

TEST_CASE("expression_evaluator_supports_logical_or_and_unary_not", "[yaha][automation]") {
    const auto orAst = parseScript("false or true");
    const yaha::ExpressionEvaluationResult orResult = yaha::ExpressionEvaluator::evaluate(
        orAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(orResult.success);
    REQUIRE(std::holds_alternative<bool>(orResult.value));
    REQUIRE(std::get<bool>(orResult.value));

    const auto unaryAst = parseScript("not false");
    const yaha::ExpressionEvaluationResult unaryResult = yaha::ExpressionEvaluator::evaluate(
        unaryAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(unaryResult.success);
    REQUIRE(std::holds_alternative<bool>(unaryResult.value));
    REQUIRE(std::get<bool>(unaryResult.value));
}

TEST_CASE("expression_evaluator_supports_neq_and_le_operators", "[yaha][automation]") {
    const auto neqAst = parseScript("awake != sleeping");
    const yaha::ExpressionEvaluationResult neqResult = yaha::ExpressionEvaluator::evaluate(
        neqAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(neqResult.success);
    REQUIRE(std::holds_alternative<bool>(neqResult.value));
    REQUIRE(std::get<bool>(neqResult.value));

    const auto leAst = parseScript(R"("07:00" <= "07:10")");
    const yaha::ExpressionEvaluationResult leResult = yaha::ExpressionEvaluator::evaluate(
        leAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(leResult.success);
    REQUIRE(std::holds_alternative<bool>(leResult.value));
    REQUIRE(std::get<bool>(leResult.value));
}

TEST_CASE("expression_evaluator_uses_numeric_variables", "[yaha][automation]") {
    const auto numericVarAst = parseScript("$SYS/value + 2");
    yaha::ExpressionEvaluator::VariableMap numericVars;
    numericVars.insert({"$SYS/value", k_numeric_one});
    const yaha::ExpressionEvaluationResult numericVarResult = yaha::ExpressionEvaluator::evaluate(numericVarAst, numericVars);
    REQUIRE(numericVarResult.success);
    REQUIRE(std::holds_alternative<double>(numericVarResult.value));
    REQUIRE(std::get<double>(numericVarResult.value) == Catch::Approx(k_numeric_three));
}

TEST_CASE("expression_evaluator_supports_time_variable_as_result", "[yaha][automation]") {
    const auto ast = parseScript("\"/time\"");

    yaha::ExpressionEvaluator::VariableMap vars;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    const auto expectedTime = std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_seven}};
    vars.insert({"/time", expectedTime});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::chrono::system_clock::time_point>(result.value));
    REQUIRE(std::get<std::chrono::system_clock::time_point>(result.value) == expectedTime);
}

TEST_CASE("expression_evaluator_handles_map_matching_for_numeric_and_quoted_keys", "[yaha][automation]") {
    const auto numericKeyAst = parseScript("state = (1: on, default: off)\nstate(1)");
    const yaha::ExpressionEvaluationResult numericKeyResult = yaha::ExpressionEvaluator::evaluate(
        numericKeyAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(numericKeyResult.success);
    REQUIRE(std::holds_alternative<std::string>(numericKeyResult.value));
    REQUIRE(std::get<std::string>(numericKeyResult.value) == "on");

    const auto quotedKeyAst = parseScript("state = ('on': awake, default: absent)\nstate(on)");
    const yaha::ExpressionEvaluationResult quotedKeyResult = yaha::ExpressionEvaluator::evaluate(
        quotedKeyAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(quotedKeyResult.success);
    REQUIRE(std::holds_alternative<std::string>(quotedKeyResult.value));
    REQUIRE(std::get<std::string>(quotedKeyResult.value) == "awake");
}

TEST_CASE("expression_evaluator_handles_if_with_string_truthiness", "[yaha][automation]") {
    const auto ast = parseScript("if(\"off\", on, off)");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "off");
}

TEST_CASE("expression_evaluator_reports_invalid_relational_operands", "[yaha][automation]") {
    const auto ast = parseScript("on > off");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("expression_evaluator_supports_time_arithmetic_for_string_time", "[yaha][automation]") {
    const auto ast = parseScript("\"07:00\" + 10");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<std::string>(result.value));
    REQUIRE(std::get<std::string>(result.value) == "07:10:00");
}

TEST_CASE("expression_evaluator_supports_hhmmss_and_rejects_invalid_time_text", "[yaha][automation]") {
    const auto validAst = parseScript(R"("07:00:05" < "07:00:10")");
    const yaha::ExpressionEvaluationResult validResult = yaha::ExpressionEvaluator::evaluate(
        validAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(validResult.success);
    REQUIRE(std::holds_alternative<bool>(validResult.value));
    REQUIRE(std::get<bool>(validResult.value));

    const auto invalidAst = parseScript(R"("07:70" < "08:00")");
    const yaha::ExpressionEvaluationResult invalidResult = yaha::ExpressionEvaluator::evaluate(
        invalidAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE_FALSE(invalidResult.success);
    REQUIRE_FALSE(invalidResult.errors.empty());
}

TEST_CASE("expression_evaluator_supports_numeric_relational_comparison", "[yaha][automation]") {
    const auto ast = parseScript("2 > 1");

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(
        ast,
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(std::holds_alternative<bool>(result.value));
    REQUIRE(std::get<bool>(result.value));
}

TEST_CASE("expression_evaluator_handles_bool_and_time_variable_refs", "[yaha][automation]") {
    const auto boolAst = parseScript("$SYS/enabled");
    yaha::ExpressionEvaluator::VariableMap boolVars;
    boolVars.insert({"$SYS/enabled", true});
    const yaha::ExpressionEvaluationResult boolResult = yaha::ExpressionEvaluator::evaluate(boolAst, boolVars);
    REQUIRE(boolResult.success);
    REQUIRE(std::holds_alternative<bool>(boolResult.value));
    REQUIRE(std::get<bool>(boolResult.value));

    const auto timeAst = parseScript("$SYS/time");
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    const auto expectedTime = std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_nine}};
    yaha::ExpressionEvaluator::VariableMap timeVars;
    timeVars.insert({"$SYS/time", expectedTime});
    const yaha::ExpressionEvaluationResult timeResult = yaha::ExpressionEvaluator::evaluate(timeAst, timeVars);
    REQUIRE(timeResult.success);
    REQUIRE(std::holds_alternative<std::chrono::system_clock::time_point>(timeResult.value));
    REQUIRE(std::get<std::chrono::system_clock::time_point>(timeResult.value) == expectedTime);
}

TEST_CASE("expression_evaluator_supports_numeric_and_time_equality", "[yaha][automation]") {
    const auto numericAst = parseScript("\"/num\" = 1");
    yaha::ExpressionEvaluator::VariableMap numericVars;
    numericVars.insert({"/num", k_numeric_one});
    const yaha::ExpressionEvaluationResult numericResult = yaha::ExpressionEvaluator::evaluate(numericAst, numericVars);
    REQUIRE(numericResult.success);
    REQUIRE(std::holds_alternative<bool>(numericResult.value));
    REQUIRE(std::get<bool>(numericResult.value));

    const auto timeAst = parseScript(R"("/time" = "09:00")");
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    yaha::ExpressionEvaluator::VariableMap timeVars;
    timeVars.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_nine}}});
    const yaha::ExpressionEvaluationResult timeResult = yaha::ExpressionEvaluator::evaluate(timeAst, timeVars);
    REQUIRE(timeResult.success);
    REQUIRE(std::holds_alternative<bool>(timeResult.value));
    REQUIRE(std::get<bool>(timeResult.value));
}

TEST_CASE("expression_evaluator_applies_numeric_truthiness", "[yaha][automation]") {
    const auto numberAst = parseScript("if(1, on, off)");
    const yaha::ExpressionEvaluationResult numberResult = yaha::ExpressionEvaluator::evaluate(
        numberAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(numberResult.success);
    REQUIRE(std::holds_alternative<std::string>(numberResult.value));
    REQUIRE(std::get<std::string>(numberResult.value) == "on");
}

TEST_CASE("expression_evaluator_applies_string_truthiness", "[yaha][automation]") {
    const auto stringAst = parseScript("if(on, yes, no)");
    const yaha::ExpressionEvaluationResult stringResult = yaha::ExpressionEvaluator::evaluate(
        stringAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(stringResult.success);
    REQUIRE(std::holds_alternative<std::string>(stringResult.value));
    REQUIRE(std::get<std::string>(stringResult.value) == "yes");
}

TEST_CASE("expression_evaluator_applies_zero_truthiness", "[yaha][automation]") {
    const auto zeroAst = parseScript("if(0, on, off)");
    const yaha::ExpressionEvaluationResult zeroResult = yaha::ExpressionEvaluator::evaluate(
        zeroAst,
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(zeroResult.success);
    REQUIRE(std::holds_alternative<std::string>(zeroResult.value));
    REQUIRE(std::get<std::string>(zeroResult.value) == "off");
}

TEST_CASE("expression_evaluator_provides_human_readable_reason", "[yaha][automation]") {
    const auto ast = parseScript("if($SYS/presence = awake, on, off)");

    yaha::ExpressionEvaluator::VariableMap vars;
    vars.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::ExpressionEvaluationResult result = yaha::ExpressionEvaluator::evaluate(ast, vars);

    REQUIRE(result.success);
    REQUIRE_FALSE(result.reason.empty());
    REQUIRE(result.reason.find("$SYS/presence (awake) is = awake") != std::string::npos);
    REQUIRE(result.reason.find("if ") != std::string::npos);
    REQUIRE(result.reason.find(" then on") != std::string::npos);
}
