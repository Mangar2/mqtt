#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <ranges>
#include <string>
#include <vector>

#include "yaha/automation_client/automation_client_component.h"
#include "yaha/automation_client/automation_rule_json.h"

namespace {

const yaha::Message* findLastMessageForTopic(
    const std::vector<yaha::Message>& messages,
    const std::string& topic) {
    for (const auto& message : std::views::reverse(messages)) {
        if (message.topic() == topic) {
            return &message;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("automation_management_update_stores_hierarchical_rule_without_flat_duplicate", "[automation_client]") {
    yaha::AutomationClientConfig config{};
    config.fileStoreEnabled = false;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/first/dressingroom/awake/set",
        std::string{
            R"json({"name":"first/dressingroom/awake","topic":"first/dressingroom/zwave/switch/dressing room/set","active":false,"check":"status/presence != initial","value":"map_1 = (default: off, awake: on)\nmap_1(status/presence)"})json"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE_FALSE(component.hasRule("first/dressingroom/awake"));
    REQUIRE_FALSE(component.hasRule("awake"));

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/first/dressingroom/awake/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    const yaha::Message* ackMessage =
        findLastMessageForTopic(published, "$MONITOR/automation/rules/first/dressingroom/awake");
    REQUIRE(ackMessage != nullptr);

    const yaha::Message* traceMessage =
        findLastMessageForTopic(published, "$MONITOR/automation/first/dressingroom/awake/trace");
    REQUIRE(traceMessage != nullptr);
    REQUIRE(std::holds_alternative<std::string>(traceMessage->value()));
    REQUIRE(std::get<std::string>(traceMessage->value()) != "error");

    component.close();
}

TEST_CASE("automation_management_delete_removes_hierarchical_rule", "[automation_client]") {
    yaha::AutomationClientConfig config{};
    config.fileStoreEnabled = false;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/first/dressingroom/awake/set",
        std::string{R"({"topic":"first/dressingroom/light/set","value":"on"})"},
        yaha::Qos::AtLeastOnce,
        false});

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/first/dressingroom/awake/set",
        std::string{"delete"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE_FALSE(component.hasRule("first/dressingroom/awake"));

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/first/dressingroom/awake/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    const yaha::Message* traceMessage =
        findLastMessageForTopic(published, "$MONITOR/automation/first/dressingroom/awake/trace");
    REQUIRE(traceMessage != nullptr);
    REQUIRE(std::holds_alternative<std::string>(traceMessage->value()));
    REQUIRE(std::get<std::string>(traceMessage->value()) == "error");

    component.close();
}

TEST_CASE("automation_management_flat_rule_path_keeps_legacy_behavior", "[automation_client]") {
    yaha::AutomationClientConfig config{};
    config.fileStoreEnabled = false;

    yaha::AutomationClientComponent component{config};
    component.run();

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{R"({"topic":"house/light/set","value":"on"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("demo"));

    component.close();
}

TEST_CASE("automation_management_topic_with_empty_segment_is_ignored", "[automation_client]") {
    yaha::AutomationClientConfig config{};
    config.fileStoreEnabled = false;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/first//awake/set",
        std::string{R"({"topic":"house/light/set","value":"on"})"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.empty());
    REQUIRE_FALSE(component.hasRule("first//awake"));

    component.close();
}

TEST_CASE("automation_management_update_persists_escaped_newline_json", "[automation_client]") {
    yaha::AutomationClientConfig config{};
    config.fileStoreEnabled = false;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/first/dressingroom/awake/set",
        std::string{R"json({"topic":"first/dressingroom/zwave/switch/dressing room/set","value":"line1\nline2"})json"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    const yaha::Message* ackMessage =
        findLastMessageForTopic(published, "$MONITOR/automation/rules/first/dressingroom/awake");
    REQUIRE(ackMessage != nullptr);
    REQUIRE(std::holds_alternative<std::string>(ackMessage->value()));

    const auto& payloadText = std::get<std::string>(ackMessage->value());
    REQUIRE(payloadText.find("line1\\nline2") != std::string::npos);
    REQUIRE(payloadText.find("line1\nline2") == std::string::npos);
    REQUIRE(yaha::automation_rule_json::parseJsonNode(payloadText).has_value());

    component.close();
}
