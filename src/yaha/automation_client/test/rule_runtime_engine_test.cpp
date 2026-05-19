#include <catch2/catch_test_macros.hpp>

#include "yaha/automation_client/rule_runtime_engine.h"

#include <array>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

namespace {

constexpr std::size_t k_motion_event_count{130U};
constexpr double k_delivery_gate_seconds{10.0};
constexpr int k_state_warmup_seconds{20};
constexpr int k_target_hour{7};
constexpr int k_target_minute{5};
constexpr int k_target_second{0};
constexpr int k_weekday_count{7};
constexpr double k_inactivity_gate_minutes{5.0};

[[nodiscard]] std::chrono::system_clock::time_point localTodayTimePoint(
    const int hourValue,
    const int minuteValue,
    const int secondValue) {
    const auto nowTimePoint = std::chrono::system_clock::now();
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(nowTimePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    const auto conversionResult = localtime_s(&localCalendarTime, &epochSeconds);
    REQUIRE(conversionResult == 0);
#else
    const auto* conversionResult = localtime_r(&epochSeconds, &localCalendarTime);
    REQUIRE(conversionResult != nullptr);
#endif

    localCalendarTime.tm_hour = hourValue;
    localCalendarTime.tm_min = minuteValue;
    localCalendarTime.tm_sec = secondValue;
    localCalendarTime.tm_isdst = -1;

    const std::time_t localEpochSeconds = std::mktime(&localCalendarTime);
    REQUIRE(localEpochSeconds != static_cast<std::time_t>(-1));
    return std::chrono::system_clock::from_time_t(localEpochSeconds);
}

[[nodiscard]] int localWeekdayIndex(const std::chrono::system_clock::time_point& timePoint) {
    const std::time_t epochSeconds = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localCalendarTime{};
#if defined(_WIN32)
    const auto conversionResult = localtime_s(&localCalendarTime, &epochSeconds);
    REQUIRE(conversionResult == 0);
#else
    const auto* conversionResult = localtime_r(&epochSeconds, &localCalendarTime);
    REQUIRE(conversionResult != nullptr);
#endif
    return localCalendarTime.tm_wday;
}

[[nodiscard]] std::string weekdayNameByIndex(const int weekdayIndex) {
    static const std::array<std::string, k_weekday_count> k_weekday_names{
        "sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    REQUIRE(weekdayIndex >= 0);
    REQUIRE(weekdayIndex < k_weekday_count);
    return k_weekday_names[static_cast<std::size_t>(weekdayIndex)];
}

[[nodiscard]] yaha::RuleTreeNode makeRuleNode(
    const std::string& topic,
    const std::string& check,
    const std::string& value) {
    yaha::RuleTreeNode::Object object{};
    object.emplace("topic", yaha::RuleTreeNode{topic});
    object.emplace("check", yaha::RuleTreeNode{check});
    object.emplace("value", yaha::RuleTreeNode{value});
    return yaha::RuleTreeNode{object};
}

} // namespace

TEST_CASE("rule_runtime_engine_validates_rule_topic_shapes", "[automation_client]") {
    CHECK_FALSE(yaha::RuleRuntimeEngine::isRuleNodeStructureValid(yaha::RuleTreeNode{true}));

    yaha::RuleTreeNode::Object noTopic{};
    noTopic.emplace("check", yaha::RuleTreeNode{"1"});
    CHECK_FALSE(yaha::RuleRuntimeEngine::isRuleNodeStructureValid(yaha::RuleTreeNode{noTopic}));

    yaha::RuleTreeNode::Object emptyTopic{};
    emptyTopic.emplace("topic", yaha::RuleTreeNode{""});
    CHECK_FALSE(yaha::RuleRuntimeEngine::isRuleNodeStructureValid(yaha::RuleTreeNode{emptyTopic}));

    yaha::RuleTreeNode::Object arrayTopic{};
    arrayTopic.emplace("topic", yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"a/b"}}});
    CHECK(yaha::RuleRuntimeEngine::isRuleNodeStructureValid(yaha::RuleTreeNode{arrayTopic}));

    yaha::RuleTreeNode::Object mapTopic{};
    mapTopic.emplace("topic", yaha::RuleTreeNode{yaha::RuleTreeNode::Object{{"a/b", yaha::RuleTreeNode{"on"}}}});
    CHECK(yaha::RuleRuntimeEngine::isRuleNodeStructureValid(yaha::RuleTreeNode{mapTopic}));
}

TEST_CASE("rule_runtime_engine_ingests_motion_events_and_trims_history", "[automation_client]") {
    yaha::RuleRuntimeEventState eventState{};
    const std::vector<std::string> motionFilters{"house/motion/#"};
    const auto baseTime = std::chrono::system_clock::now();

    yaha::RuleRuntimeEngine::ingestDomainMessageEvent(
        yaha::Message{"house/motion/x", 0.0},
        baseTime,
        motionFilters,
        &eventState);
    CHECK(eventState.motionEvents.empty());

    yaha::RuleRuntimeEngine::ingestDomainMessageEvent(
        yaha::Message{"house/switch/a", std::string{"1"}},
        baseTime,
        motionFilters,
        &eventState);
    CHECK(eventState.nonMotionEvents.contains("house/switch/a"));

    for (std::size_t index = 0U; index < k_motion_event_count; ++index) {
        const std::string topic = "house/motion/" + std::to_string(index);
        yaha::RuleRuntimeEngine::ingestDomainMessageEvent(
            yaha::Message{topic, std::string{"1"}},
            baseTime + std::chrono::seconds{static_cast<int>(index)},
            motionFilters,
            &eventState);
    }

    CHECK(eventState.motionEvents.size() <= 100U);
    CHECK(eventState.nextSequenceNumber == 131U);

    yaha::RuleRuntimeEngine::clearNonMotionEvents(&eventState);
    CHECK(eventState.nonMotionEvents.empty());
}

TEST_CASE("rule_runtime_engine_reports_time_gate_errors_and_previews_delivery", "[automation_client]") {
    yaha::RuleTreeNode::Object rules{};

    yaha::RuleTreeNode badType = makeRuleNode("house/light/set", "1", "on");
    yaha::RuleTreeNode::Object badTypeObj = badType.asObject();
    badTypeObj["time"] = yaha::RuleTreeNode{1.0};
    rules.emplace("badType", yaha::RuleTreeNode{badTypeObj});

    yaha::RuleTreeNode::Object badParseObj = makeRuleNode("house/light/set", "1", "on").asObject();
    badParseObj["time"] = yaha::RuleTreeNode{"("};
    rules.emplace("badParse", yaha::RuleTreeNode{badParseObj});

    yaha::RuleTreeNode::Object unsupportedObj = makeRuleNode("house/light/set", "1", "on").asObject();
    unsupportedObj["time"] = yaha::RuleTreeNode{"1"};
    unsupportedObj["duration"] = yaha::RuleTreeNode{"00:10"};
    rules.emplace("unsupported", yaha::RuleTreeNode{unsupportedObj});

    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{{"rules", yaha::RuleTreeNode{rules}}}};

    yaha::RuleRuntimeEventState eventState{};
    yaha::RuleRuntimeDeliveryState deliveryState{};
    yaha::ExpressionEvaluator::VariableMap variables{};

    const auto result = yaha::RuleRuntimeEngine::processRules(
        root,
        variables,
        std::chrono::system_clock::now(),
        &eventState,
        &deliveryState);

    CHECK_FALSE(result.success);
    CHECK_FALSE(result.errors.empty());

    yaha::RuleTreeNode::Object previewRuleObj = makeRuleNode("house/light/set", "1", "on").asObject();
    previewRuleObj["delayInSeconds"] = yaha::RuleTreeNode{k_delivery_gate_seconds};
    previewRuleObj["cooldownInSeconds"] = yaha::RuleTreeNode{k_delivery_gate_seconds};
    yaha::RuleTreeNode previewRule{previewRuleObj};

    std::vector<yaha::Message> candidates{};
    candidates.emplace_back("house/light/set", std::string{"on"});

    const auto now = std::chrono::system_clock::now();
    const auto firstPreview = yaha::RuleRuntimeEngine::previewDeliveredMessages(
        "rules/preview",
        previewRule,
        candidates,
        now,
        yaha::RuleRuntimeDeliveryState{});
    CHECK(firstPreview.empty());

    yaha::RuleRuntimeDeliveryState warmedState{};
    yaha::RuleRuntimeDeliveryState::OutputState outputState{};
    outputState.candidateHash = "house/light/set|s:on";
    outputState.candidateSince = now - std::chrono::seconds{k_state_warmup_seconds};
    outputState.emittedHash = std::string{"house/light/set|s:on"};
    outputState.emittedAt = now - std::chrono::seconds{k_state_warmup_seconds};
    warmedState.outputStatesByKey.emplace("rules/preview|house/light/set", outputState);

    const auto secondPreview = yaha::RuleRuntimeEngine::previewDeliveredMessages(
        "rules/preview",
        previewRule,
        candidates,
        now,
        warmedState);
    CHECK(secondPreview.size() == 1U);
}

TEST_CASE("rule_runtime_engine_covers_time_duration_and_weekday_gates", "[automation_client]") {
    const auto evaluationTime = localTodayTimePoint(k_target_hour, k_target_minute, k_target_second);
    yaha::ExpressionEvaluator::VariableMap variables{};
    variables.insert({"/time", evaluationTime});

    const int weekdayValue = localWeekdayIndex(evaluationTime);
    const int weekdayMismatch = (weekdayValue + 1) % k_weekday_count;

    yaha::RuleTreeNode::Object matchingWeekdayRule = makeRuleNode("house/light/a", "1", "on").asObject();
    matchingWeekdayRule["time"] = yaha::RuleTreeNode{"\"07:00\""};
    matchingWeekdayRule["duration"] = yaha::RuleTreeNode{"00:10"};
    matchingWeekdayRule["weekdays"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{
        yaha::RuleTreeNode{weekdayNameByIndex(weekdayValue)}}};

    yaha::RuleTreeNode::Object hhmmssRule = makeRuleNode("house/light/b", "1", "on").asObject();
    hhmmssRule["time"] = yaha::RuleTreeNode{"\"07:00:00\""};
    hhmmssRule["duration"] = yaha::RuleTreeNode{"00:10"};

    yaha::RuleTreeNode::Object badDurationFallbackRule = makeRuleNode("house/light/c", "1", "on").asObject();
    badDurationFallbackRule["time"] = yaha::RuleTreeNode{"\"07:00\""};
    badDurationFallbackRule["duration"] = yaha::RuleTreeNode{"bad"};

    yaha::RuleTreeNode::Object inactiveRule = makeRuleNode("house/light/d", "1", "on").asObject();
    inactiveRule["active"] = yaha::RuleTreeNode{false};

    yaha::RuleTreeNode::Object mismatchWeekdayRule = makeRuleNode("house/light/e", "1", "on").asObject();
    mismatchWeekdayRule["weekdays"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{
        yaha::RuleTreeNode{weekdayNameByIndex(weekdayMismatch)}}};

    yaha::RuleTreeNode::Object invalidWeekdayRule = makeRuleNode("house/light/f", "1", "on").asObject();
    invalidWeekdayRule["weekdays"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{
        yaha::RuleTreeNode{"FUNDAY"}}};

    yaha::RuleTreeNode::Object invalidTimeTextRule = makeRuleNode("house/light/g", "1", "on").asObject();
    invalidTimeTextRule["time"] = yaha::RuleTreeNode{"\"25:70\""};

    yaha::RuleTreeNode::Object missingTimeVariableRule = makeRuleNode("house/light/h", "1", "on").asObject();
    missingTimeVariableRule["time"] = yaha::RuleTreeNode{"\"/missing\""};

    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{{
        "rules", yaha::RuleTreeNode{yaha::RuleTreeNode::Object{
            {"weekday_match", yaha::RuleTreeNode{matchingWeekdayRule}},
            {"hhmmss", yaha::RuleTreeNode{hhmmssRule}},
            {"duration_fallback", yaha::RuleTreeNode{badDurationFallbackRule}},
            {"inactive", yaha::RuleTreeNode{inactiveRule}},
            {"weekday_mismatch", yaha::RuleTreeNode{mismatchWeekdayRule}},
            {"weekday_invalid", yaha::RuleTreeNode{invalidWeekdayRule}},
            {"time_invalid", yaha::RuleTreeNode{invalidTimeTextRule}},
            {"time_missing_var", yaha::RuleTreeNode{missingTimeVariableRule}},
        }}}
    }};

    yaha::RuleRuntimeEventState eventState{};
    yaha::RuleRuntimeDeliveryState deliveryState{};
    const auto result = yaha::RuleRuntimeEngine::processRules(
        root,
        variables,
        evaluationTime,
        &eventState,
        &deliveryState);

    CHECK_FALSE(result.success);
    CHECK(result.messages.size() >= 4U);
    CHECK(result.errors.size() >= 2U);
}

TEST_CASE("rule_runtime_engine_covers_event_gates_allow_noneof_and_inactivity", "[automation_client]") {
    const auto evaluationTime = std::chrono::system_clock::now();
    yaha::ExpressionEvaluator::VariableMap variables{};

    yaha::RuleRuntimeEventState eventState{};
    eventState.nonMotionEvents.insert("house/switch/a");
    eventState.nonMotionEvents.insert("house/block/x");
    eventState.motionEvents.push_back(yaha::MotionEventRecord{
        .topicName = "house/motion/a",
        .timestamp = evaluationTime - std::chrono::seconds{2},
        .sequenceNumber = 1U,
    });

    yaha::RuleTreeNode::Object anyOfPassRule = makeRuleNode("house/out/a", "1", "on").asObject();
    anyOfPassRule["anyOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/switch/#"}}};

    yaha::RuleTreeNode::Object allOfPassRule = makeRuleNode("house/out/b", "1", "on").asObject();
    allOfPassRule["allOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{
        yaha::RuleTreeNode{"house/switch/#"},
        yaha::RuleTreeNode{"house/motion/#"}}};

    yaha::RuleTreeNode::Object noneOfBlockedRule = makeRuleNode("house/out/c", "1", "on").asObject();
    noneOfBlockedRule["anyOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/switch/#"}}};
    noneOfBlockedRule["noneOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/block/#"}}};

    yaha::RuleTreeNode::Object allowBlockedRule = makeRuleNode("house/out/d", "1", "on").asObject();
    allowBlockedRule["anyOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/switch/#"}}};
    allowBlockedRule["allow"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/motion/allowed/#"}}};

    yaha::RuleTreeNode::Object inactivityBlockedRule = makeRuleNode("house/out/e", "1", "on").asObject();
    inactivityBlockedRule["anyOf"] = yaha::RuleTreeNode{yaha::RuleTreeNode::Array{yaha::RuleTreeNode{"house/switch/#"}}};
    inactivityBlockedRule["durationWithoutMovementInMinutes"] = yaha::RuleTreeNode{k_inactivity_gate_minutes};

    yaha::RuleTreeNode root{yaha::RuleTreeNode::Object{{
        "rules", yaha::RuleTreeNode{yaha::RuleTreeNode::Object{
            {"anyof_pass", yaha::RuleTreeNode{anyOfPassRule}},
            {"allof_pass", yaha::RuleTreeNode{allOfPassRule}},
            {"noneof_block", yaha::RuleTreeNode{noneOfBlockedRule}},
            {"allow_block", yaha::RuleTreeNode{allowBlockedRule}},
            {"inactivity_block", yaha::RuleTreeNode{inactivityBlockedRule}},
        }}}
    }};

    yaha::RuleRuntimeDeliveryState deliveryState{};
    const auto result = yaha::RuleRuntimeEngine::processRules(
        root,
        variables,
        evaluationTime,
        &eventState,
        &deliveryState);

    CHECK(result.success);
    CHECK(result.messages.size() == 2U);
}
