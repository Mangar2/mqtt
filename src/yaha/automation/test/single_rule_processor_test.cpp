#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation/single_rule_processor.h"

namespace {

constexpr double k_temperature_setpoint{21.5};
constexpr double k_invalid_qos_value{3.0};
constexpr double k_qos_zero{0.0};
constexpr double k_qos_two{2.0};
constexpr int k_time_hour_seven{7};

} // namespace

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

TEST_CASE("single_rule_processor_accepts_numeric_value", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/temp/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{k_temperature_setpoint}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(result.message.has_value());
    REQUIRE(std::holds_alternative<double>(result.message->value()));
}

TEST_CASE("single_rule_processor_accepts_boolean_value", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/flag/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{true}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(result.message.has_value());
    REQUIRE(std::holds_alternative<std::string>(result.message->value()));
    REQUIRE(std::get<std::string>(result.message->value()) == "true");
}

TEST_CASE("single_rule_processor_reports_non_string_check", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"check", yaha::RuleTreeNode{1.0}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_reports_missing_value", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_reports_invalid_qos_value", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});
    ruleObject.insert({"qos", yaha::RuleTreeNode{k_invalid_qos_value}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_reports_non_numeric_qos", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});
    ruleObject.insert({"qos", yaha::RuleTreeNode{"one"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_rejects_non_object_rule_node", "[yaha][automation]") {
    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{1.0},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_rejects_empty_topic", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{""}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_handles_numeric_and_string_check_truthiness", "[yaha][automation]") {
    yaha::RuleTreeNode::Object numericCheckRule;
    numericCheckRule.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    numericCheckRule.insert({"check", yaha::RuleTreeNode{"1"}});
    numericCheckRule.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult numericCheckResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(numericCheckRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(numericCheckResult.success);
    REQUIRE(numericCheckResult.triggered);
    REQUIRE(numericCheckResult.message.has_value());

    yaha::RuleTreeNode::Object stringCheckRule;
    stringCheckRule.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    stringCheckRule.insert({"check", yaha::RuleTreeNode{"\"  off  \""}});
    stringCheckRule.insert({"value", yaha::RuleTreeNode{"on"}});

    const yaha::SingleRuleProcessingResult stringCheckResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(stringCheckRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(stringCheckResult.success);
    REQUIRE_FALSE(stringCheckResult.triggered);
    REQUIRE_FALSE(stringCheckResult.message.has_value());
}

TEST_CASE("single_rule_processor_maps_expression_value_types_to_message", "[yaha][automation]") {
    yaha::RuleTreeNode::Object numericValueRule;
    numericValueRule.insert({"topic", yaha::RuleTreeNode{"home/value/set"}});
    numericValueRule.insert({"value", yaha::RuleTreeNode{"1 + 2"}});

    const yaha::SingleRuleProcessingResult numericValueResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(numericValueRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(numericValueResult.success);
    REQUIRE(numericValueResult.message.has_value());
    REQUIRE(std::holds_alternative<double>(numericValueResult.message->value()));

    yaha::RuleTreeNode::Object boolValueRule;
    boolValueRule.insert({"topic", yaha::RuleTreeNode{"home/value/set"}});
    boolValueRule.insert({"value", yaha::RuleTreeNode{"true"}});

    const yaha::SingleRuleProcessingResult boolValueResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(boolValueRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(boolValueResult.success);
    REQUIRE(boolValueResult.message.has_value());
    REQUIRE(std::holds_alternative<std::string>(boolValueResult.message->value()));
    REQUIRE(std::get<std::string>(boolValueResult.message->value()) == "true");
}

TEST_CASE("single_rule_processor_reports_value_evaluation_error", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"$missing"}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_reports_time_expression_value_as_unsupported", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"\"/time\""}});

    yaha::ExpressionEvaluator::VariableMap variables;
    const auto day = std::chrono::sys_days{std::chrono::year{2026} / std::chrono::May / 4};
    variables.insert({"/time", std::chrono::system_clock::time_point{day + std::chrono::hours{k_time_hour_seven}}});

    const yaha::SingleRuleProcessingResult result = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(ruleObject)},
        variables);

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("single_rule_processor_accepts_qos_zero_and_two", "[yaha][automation]") {
    yaha::RuleTreeNode::Object qosZeroRule;
    qosZeroRule.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    qosZeroRule.insert({"value", yaha::RuleTreeNode{"on"}});
    qosZeroRule.insert({"qos", yaha::RuleTreeNode{k_qos_zero}});

    const yaha::SingleRuleProcessingResult qosZeroResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(qosZeroRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(qosZeroResult.success);
    REQUIRE(qosZeroResult.message.has_value());
    REQUIRE(qosZeroResult.message->qos() == yaha::Qos::AtMostOnce);

    yaha::RuleTreeNode::Object qosTwoRule;
    qosTwoRule.insert({"topic", yaha::RuleTreeNode{"home/light/set"}});
    qosTwoRule.insert({"value", yaha::RuleTreeNode{"on"}});
    qosTwoRule.insert({"qos", yaha::RuleTreeNode{k_qos_two}});

    const yaha::SingleRuleProcessingResult qosTwoResult = yaha::SingleRuleProcessor::process(
        yaha::RuleTreeNode{std::move(qosTwoRule)},
        yaha::ExpressionEvaluator::VariableMap{});
    REQUIRE(qosTwoResult.success);
    REQUIRE(qosTwoResult.message.has_value());
    REQUIRE(qosTwoResult.message->qos() == yaha::Qos::ExactlyOnce);
}
// NOLINTEND(readability-function-cognitive-complexity)
