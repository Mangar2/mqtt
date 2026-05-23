#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "yaha/automation_client/automation_rule_tree_access.h"

TEST_CASE("automation_rule_tree_access_upsert_and_find_nested_rule", "[automation_client]") {
    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{}};
    const std::vector<std::string> rulePath{"first", "dressingroom", "awake"};

    yaha::RuleTreeNode::Object ruleObject{};
    ruleObject.insert({"topic", yaha::RuleTreeNode{std::string{"house/light/set"}}});
    ruleObject.insert({"value", yaha::RuleTreeNode{std::string{"on"}}});

    yaha::automation_rule_tree_access::upsertRuleByPath(&root, rulePath, yaha::RuleTreeNode{std::move(ruleObject)});

    const yaha::RuleTreeNode* ruleNode = yaha::automation_rule_tree_access::findRuleByPath(root, rulePath);
    REQUIRE(ruleNode != nullptr);
    REQUIRE(ruleNode->isObject());
    REQUIRE(ruleNode->asObject().contains("topic"));
    REQUIRE(ruleNode->asObject().contains("value"));
}

TEST_CASE("automation_rule_tree_access_erase_rule_by_path", "[automation_client]") {
    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{}};
    const std::vector<std::string> rulePath{"first", "dressingroom", "awake"};

    yaha::RuleTreeNode::Object ruleObject{};
    ruleObject.insert({"topic", yaha::RuleTreeNode{std::string{"house/light/set"}}});
    yaha::automation_rule_tree_access::upsertRuleByPath(&root, rulePath, yaha::RuleTreeNode{std::move(ruleObject)});

    REQUIRE(yaha::automation_rule_tree_access::eraseRuleByPath(&root, rulePath));
    REQUIRE(yaha::automation_rule_tree_access::findRuleByPath(root, rulePath) == nullptr);
    REQUIRE_FALSE(yaha::automation_rule_tree_access::eraseRuleByPath(&root, rulePath));
}

TEST_CASE("automation_rule_tree_access_find_rule_container_handles_missing_path", "[automation_client]") {
    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{}};

    const std::vector<std::string> missingParentPath{"first", "dressingroom"};
    REQUIRE(yaha::automation_rule_tree_access::findRuleContainerByPath(&root, missingParentPath) == nullptr);

    yaha::RuleTreeNode::Object* container =
        yaha::automation_rule_tree_access::ensureRuleContainerByPath(&root, missingParentPath);
    REQUIRE(container != nullptr);
    REQUIRE(container->empty());

    REQUIRE(yaha::automation_rule_tree_access::findRuleContainerByPath(&root, missingParentPath) != nullptr);
}

TEST_CASE("automation_rule_tree_access_empty_rule_path_is_safe", "[automation_client]") {
    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{}};
    const std::vector<std::string> emptyPath{};

    yaha::automation_rule_tree_access::upsertRuleByPath(&root, emptyPath, yaha::RuleTreeNode{true});
    REQUIRE(yaha::automation_rule_tree_access::findRuleByPath(root, emptyPath) == nullptr);
    REQUIRE_FALSE(yaha::automation_rule_tree_access::eraseRuleByPath(&root, emptyPath));
}
