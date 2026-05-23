#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "yaha/automation_client/automation_rule_json.h"

namespace {

std::string buildControlString() {
    std::string value{"prefix"};
    value.push_back('\n');
    value.push_back('\r');
    value.push_back('\t');
    value.push_back('\b');
    value.push_back('\f');
    value.push_back(static_cast<char>(0x01));
    value += "suffix";
    return value;
}

void requireEscapedControlSequences(const std::string& jsonText) {
    constexpr std::array<const char*, 6U> expectedEscapes{
        "\\n",
        "\\r",
        "\\t",
        "\\b",
        "\\f",
        "\\u0001"};
    for (const auto* escapeText : expectedEscapes) {
        REQUIRE(jsonText.find(escapeText) != std::string::npos);
    }
    REQUIRE(jsonText.find('\n') == std::string::npos);
}

} // namespace

TEST_CASE("automation_rule_json_to_json_text_escapes_control_characters", "[automation_client]") {
    yaha::RuleTreeNode::Object ruleObject{};
    ruleObject.insert({"topic", yaha::RuleTreeNode{std::string{"house/light/set"}}});
    ruleObject.insert({"value", yaha::RuleTreeNode{buildControlString()}});

    const yaha::RuleTreeNode node{std::move(ruleObject)};
    const std::string jsonText = yaha::automation_rule_json::toJsonText(node);

    REQUIRE(yaha::automation_rule_json::parseJsonNode(jsonText).has_value());
    requireEscapedControlSequences(jsonText);
}

TEST_CASE("automation_rule_json_roundtrip_preserves_newline_as_json_escape", "[automation_client]") {
    const std::string payload =
        R"({"topic":"first/dressingroom/zwave/switch/dressing room/set","value":"line1\nline2"})";

    const std::optional<yaha::RuleTreeNode> parsed = yaha::automation_rule_json::parseJsonNode(payload);
    REQUIRE(parsed.has_value());

    const std::string serialized = yaha::automation_rule_json::toJsonText(*parsed);
    REQUIRE(yaha::automation_rule_json::parseJsonNode(serialized).has_value());
    REQUIRE(serialized.find("line1\\nline2") != std::string::npos);
    REQUIRE(serialized.find("line1\nline2") == std::string::npos);
}
