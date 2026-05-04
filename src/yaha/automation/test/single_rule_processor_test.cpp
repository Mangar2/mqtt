#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation/single_rule_processor.h"

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("single_rule_processor_processes_complete_rule_and_emits_message", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"check", yaha::RuleTreeNode{"$SYS/presence = awake"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"if($SYS/presence = awake, on, off)"}});
    ruleObject.insert({"qos", yaha::RuleTreeNode{1.0}});

    yaha::ExpressionEvaluator::VariableMap variables;
    variables.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        variables);

    REQUIRE(result.success);
    REQUIRE(result.triggered);
    REQUIRE(result.errors.empty());
    REQUIRE(result.message.has_value());
    REQUIRE(result.message->topic() == "home/light/set");
    REQUIRE(std::holds_alternative<std::string>(result.message->value()));
    REQUIRE(std::get<std::string>(result.message->value()) == "on");
    REQUIRE(result.message->qos() == yaha::Qos::AtLeastOnce);
    REQUIRE(result.usedVariables.contains("$SYS/presence"));
}

TEST_CASE("single_rule_processor_returns_no_message_when_check_is_false", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"check", yaha::RuleTreeNode{"$SYS/presence = sleeping"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});

    yaha::ExpressionEvaluator::VariableMap variables;
    variables.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        variables);

    REQUIRE(result.success);
    REQUIRE_FALSE(result.triggered);
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.message.has_value());
}

TEST_CASE("single_rule_processor_reports_invalid_rule_structure", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.errors.empty() == false);
    REQUIRE(result.message.has_value() == false);
}

TEST_CASE("single_rule_processor_reports_expression_errors", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"check", yaha::RuleTreeNode{"if(a, b)"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.errors.empty() == false);
    REQUIRE_FALSE(result.message.has_value());
}
// NOLINTEND(readability-function-cognitive-complexity)
