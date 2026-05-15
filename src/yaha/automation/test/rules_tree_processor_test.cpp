#include <catch2/catch_test_macros.hpp>

#include <string>

#include "yaha/automation/rules_tree_parser.h"
#include "yaha/automation/rules_tree_processor.h"

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("rules_tree_processor_processes_all_rules_in_tree", "[yaha][automation]") {
    yaha::RuleTreeNode::Object firstRule;
    firstRule.insert({"topic", yaha::RuleTreeNode{"house/light/set"}});
    firstRule.insert({"check", yaha::RuleTreeNode{"$SYS/presence = awake"}});
    firstRule.insert({"value", yaha::RuleTreeNode{"on"}});

    yaha::RuleTreeNode::Object secondRule;
    secondRule.insert({"topic", yaha::RuleTreeNode{"house/heating/set"}});
    secondRule.insert({"check", yaha::RuleTreeNode{"$SYS/presence = sleeping"}});
    secondRule.insert({"value", yaha::RuleTreeNode{"off"}});

    yaha::RuleTreeNode::Object rulesObject;
    rulesObject.insert({"wakeUpRule", yaha::RuleTreeNode{std::move(firstRule)}});
    rulesObject.insert({"nightRule", yaha::RuleTreeNode{std::move(secondRule)}});

    yaha::RuleTreeNode::Object rootObject;
    rootObject.insert({"rules", yaha::RuleTreeNode{std::move(rulesObject)}});

    yaha::ExpressionEvaluator::VariableMap variables;
    variables.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::RulesTreeProcessingResult result = yaha::RulesTreeProcessor::process(
        yaha::RuleTreeNode{std::move(rootObject)},
        variables);

    REQUIRE(result.success);
    REQUIRE(result.errors.empty());
    REQUIRE(result.processedRules == 2U);
    REQUIRE(result.triggeredRules == 1U);
    REQUIRE(result.messages.size() == 1U);
    REQUIRE(result.messages.front().topic() == "house/light/set");
    REQUIRE(std::holds_alternative<std::string>(result.messages.front().value()));
    REQUIRE(std::get<std::string>(result.messages.front().value()) == "on");
    REQUIRE(result.usedVariables.contains("$SYS/presence"));
}

TEST_CASE("rules_tree_processor_emits_all_messages_for_topic_object_map", "[yaha][automation]") {
    yaha::RuleTreeNode::Object topicMap;
    topicMap.insert({"ground/livingroom/zwave/shutter/southeast/set", yaha::RuleTreeNode{"on"}});
    topicMap.insert({"ground/livingroom/zwave/shutter/southwest/set", yaha::RuleTreeNode{"on"}});

    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{std::move(topicMap)}});

    yaha::RuleTreeNode::Object rulesObject;
    rulesObject.insert({"openShutters", yaha::RuleTreeNode{std::move(ruleObject)}});

    yaha::RuleTreeNode::Object rootObject;
    rootObject.insert({"rules", yaha::RuleTreeNode{std::move(rulesObject)}});

    const yaha::RulesTreeProcessingResult result = yaha::RulesTreeProcessor::process(
        yaha::RuleTreeNode{std::move(rootObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE(result.success);
    REQUIRE(result.processedRules == 1U);
    REQUIRE(result.triggeredRules == 1U);
    REQUIRE(result.messages.size() == 2U);
}

TEST_CASE("rules_tree_processor_collects_path_aware_errors_and_keeps_valid_results", "[yaha][automation]") {
    yaha::RuleTreeNode::Object validRule;
    validRule.insert({"topic", yaha::RuleTreeNode{"house/light/set"}});
    validRule.insert({"check", yaha::RuleTreeNode{"true"}});
    validRule.insert({"value", yaha::RuleTreeNode{"on"}});

    yaha::RuleTreeNode::Object invalidRule;
    invalidRule.insert({"topic", yaha::RuleTreeNode{"house/faulty/set"}});
    invalidRule.insert({"value", yaha::RuleTreeNode{yaha::RuleTreeNode::Object{}}});

    yaha::RuleTreeNode::Object rulesObject;
    rulesObject.insert({"good", yaha::RuleTreeNode{std::move(validRule)}});
    rulesObject.insert({"bad", yaha::RuleTreeNode{std::move(invalidRule)}});

    yaha::RuleTreeNode::Object rootObject;
    rootObject.insert({"block", yaha::RuleTreeNode{std::move(rulesObject)}});

    const yaha::RulesTreeProcessingResult result = yaha::RulesTreeProcessor::process(
        yaha::RuleTreeNode{std::move(rootObject)},
        yaha::ExpressionEvaluator::VariableMap{});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.processedRules == 2U);
    REQUIRE(result.triggeredRules == 1U);
    REQUIRE(result.messages.size() == 1U);
    REQUIRE(result.errors.empty() == false);
}

TEST_CASE("rules_tree_processor_emitted_messages_include_evaluation_trace_reason", "[yaha][automation]") {
    yaha::RuleTreeNode::Object ruleObject;
    ruleObject.insert({"topic", yaha::RuleTreeNode{"house/light/set"}});
    ruleObject.insert({"check", yaha::RuleTreeNode{"$SYS/presence = awake"}});
    ruleObject.insert({"value", yaha::RuleTreeNode{"if($SYS/presence = awake, on, off)"}});

    yaha::RuleTreeNode::Object rulesObject;
    rulesObject.insert({"wakeUpRule", yaha::RuleTreeNode{std::move(ruleObject)}});

    yaha::RuleTreeNode::Object rootObject;
    rootObject.insert({"rules", yaha::RuleTreeNode{std::move(rulesObject)}});

    yaha::ExpressionEvaluator::VariableMap variables;
    variables.insert({"$SYS/presence", std::string{"awake"}});

    const yaha::RulesTreeProcessingResult result = yaha::RulesTreeProcessor::process(
        yaha::RuleTreeNode{std::move(rootObject)},
        variables);

    REQUIRE(result.success);
    REQUIRE(result.messages.size() == 1U);
    REQUIRE(result.messages.front().reason().size() == 1U);

    const std::string& summaryMessage = result.messages.front().reason().front().message;
    REQUIRE(summaryMessage.starts_with("Rule: house/light/set"));
}
// NOLINTEND(readability-function-cognitive-complexity)
