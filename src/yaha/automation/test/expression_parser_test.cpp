#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/automation/expression_parser.h"
#include "yaha/automation/rules_tree_parser.h"

TEST_CASE("expression_parser_parses_declarations_and_collects_external_topics", "[yaha][automation]") {
    const std::string script =
        "presence = (1: awake, on: awake, default: absent)\n"
        "$MONITORING/presence != presence($MONITORING/presence/set)";

    const yaha::ExpressionParseResult result = yaha::ExpressionParser::parse(script);

    REQUIRE(result.success);
    REQUIRE(result.errors.empty());
    REQUIRE(result.ast.declarations.size() == 1U);
    REQUIRE(result.ast.declarations[0].name == "presence");

    REQUIRE(result.externalVariables.contains("$MONITORING/presence"));
    REQUIRE(result.externalVariables.contains("$MONITORING/presence/set"));
    REQUIRE(result.externalVariables.size() == 2U);
}

TEST_CASE("expression_parser_builds_operator_precedence_tree", "[yaha][automation]") {
    const std::string script = "a = b and c = d or not e = f";

    const yaha::ExpressionParseResult result = yaha::ExpressionParser::parse(script);

    REQUIRE(result.success);
    REQUIRE(result.ast.resultExpression != nullptr);

    REQUIRE(std::holds_alternative<yaha::BinaryOpNode>(result.ast.resultExpression->node));
    const auto& top = std::get<yaha::BinaryOpNode>(result.ast.resultExpression->node);
    REQUIRE(top.op == yaha::BinaryOperator::Or);
}

TEST_CASE("rules_tree_parser_exposes_snippets_by_slash_path", "[yaha][automation]") {
    using Node = yaha::RuleTreeNode;

    const Node root{Node::Object{
        {"motion", Node{Node::Object{
            {"rules", Node{Node::Object{
                {"setReceived", Node{Node::Object{
                    {"check", Node{"$MONITORING/presence != state($MONITORING/presence/set)"}},
                    {"value", Node{"if($MONITORING/presence = awake, on, off)"}}
                }}}
            }}}
        }}}
    }};

    const yaha::RulesTreeParseResult result = yaha::RulesTreeParser::parse(root);

    REQUIRE(result.success());
    REQUIRE(result.snippetsByPath.contains("motion/rules/setReceived/check"));
    REQUIRE(result.snippetsByPath.contains("motion/rules/setReceived/value"));

    REQUIRE(result.externalVariables.contains("$MONITORING/presence"));
    REQUIRE(result.externalVariables.contains("$MONITORING/presence/set"));
}

TEST_CASE("rules_tree_parser_reports_path_on_expression_error", "[yaha][automation]") {
    using Node = yaha::RuleTreeNode;

    const Node root{Node::Object{
        {"motion", Node{Node::Object{
            {"rules", Node{Node::Object{
                {"broken", Node{Node::Object{
                    {"check", Node{"if(a, b)"}}
                }}}
            }}}
        }}}
    }};

    const yaha::RulesTreeParseResult result = yaha::RulesTreeParser::parse(root);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.errors.empty() == false);
    REQUIRE(result.errors[0].sourcePath == "motion/rules/broken/check");
}

TEST_CASE("expression_parser_accepts_quoted_time_variables_in_conditions", "[yaha][automation]") {
    const std::string script = "\"/time\" >= $SYS/motion/cellar/latest + 5 and $SYS/motion/ground/latest > $SYS/motion/cellar/latest";

    const yaha::ExpressionParseResult result = yaha::ExpressionParser::parse(script);

    REQUIRE(result.success);
}

TEST_CASE("expression_parser_accepts_if_condition_with_spaced_topic_names", "[yaha][automation]") {
    const std::string script = "if(\"/time\" > \"/sunset\" + outdoor/garden/light stairs/sunset delay and \"/time\" < outdoor/garden/light stairs/off evening, on, off)";

    const yaha::ExpressionParseResult result = yaha::ExpressionParser::parse(script);

    REQUIRE(result.success);
}
