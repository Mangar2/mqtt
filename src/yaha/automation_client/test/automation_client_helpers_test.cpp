#include <catch2/catch_test_macros.hpp>

#include "yaha/automation_client/automation_message_values.h"
#include "yaha/automation_client/automation_publish_failure_text.h"
#include "yaha/automation_client/automation_trace_format.h"

#include <string>
#include <vector>

namespace {

constexpr double k_numeric_value{12.5};
constexpr double k_numeric_trace_value{3.5};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("automation_publish_failure_text_maps_all_categories", "[automation_client]") {
    using yaha::PublishFailureCategory;

    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::None) == "none");
    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::Disconnected) == "disconnected");
    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::AckTimeout) == "ack_timeout");
    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::WriteFailed) == "write_failed");
    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::CallbackMissing) == "callback_missing");
    CHECK(yaha::automation_publish_failure_text::toText(PublishFailureCategory::Unknown) == "unknown");
    CHECK(yaha::automation_publish_failure_text::toText(static_cast<PublishFailureCategory>(255)) == "unknown");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("automation_message_values_converts_message_and_qos_values", "[automation_client]") {
    const yaha::Value textValue{std::string{"alpha"}};
    const yaha::Value numericValue{k_numeric_value};

    CHECK(std::holds_alternative<std::string>(yaha::automation_message_values::toExpressionValue(textValue)));
    CHECK(std::get<std::string>(yaha::automation_message_values::toExpressionValue(textValue)) == "alpha");
    CHECK(std::holds_alternative<double>(yaha::automation_message_values::toExpressionValue(numericValue)));
    CHECK(std::get<double>(yaha::automation_message_values::toExpressionValue(numericValue)) == k_numeric_value);

    CHECK(yaha::automation_message_values::valueToLogText(textValue) == "alpha");
    CHECK(yaha::automation_message_values::valueToLogText(numericValue).find("12.5") != std::string::npos);

    CHECK(yaha::automation_message_values::qosToLogText(yaha::Qos::AtMostOnce) == "0");
    CHECK(yaha::automation_message_values::qosToLogText(yaha::Qos::AtLeastOnce) == "1");
    CHECK(yaha::automation_message_values::qosToLogText(yaha::Qos::ExactlyOnce) == "2");
    CHECK(yaha::automation_message_values::qosToLogText(static_cast<yaha::Qos>(255)) == "unknown");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("automation_trace_format_builds_trace_entries_and_payload", "[automation_client]") {
    std::vector<std::string> traceEntries{};

    yaha::automation_trace_format::appendTraceEntry(nullptr, "ignored");
    yaha::automation_trace_format::appendTraceEntry(&traceEntries, "line-one");
    REQUIRE(traceEntries.size() == 1U);
    CHECK(traceEntries.front() == "line-one");

    const std::vector<std::string> evaluationTrace{
        "rule-evaluation:rule=my_rule",
        "rule-evaluation:topic=house/light",
        "rule-evaluation:check reason=match",
        "rule-evaluation:value reason=on",
        "rule-evaluation:error parse failed"};
    yaha::automation_trace_format::appendExplainTraceEntries(&traceEntries, evaluationTrace, "");

    REQUIRE(traceEntries.size() == 3U);
    CHECK(traceEntries[1] == "debug:error parse failed");
    CHECK(traceEntries[2] == "debug:explain Rule: my_rule, check: match, value: on");

    const std::vector<std::string> fallbackOnlyTrace{
        "rule-evaluation:check reason=matched-by-fallback"};
    yaha::automation_trace_format::appendExplainTraceEntries(&traceEntries, fallbackOnlyTrace, "fallback-rule");
    CHECK(traceEntries.back() == "debug:explain Rule: fallback-rule, check: matched-by-fallback");

    const std::vector<std::string> topicOnlyTrace{
        "rule-evaluation:topic=fallback/topic",
        "rule-evaluation:check reason=check-only"};
    yaha::automation_trace_format::appendExplainTraceEntries(&traceEntries, topicOnlyTrace, "");
    CHECK(traceEntries.back() == "debug:explain Rule: fallback/topic, check: check-only");

    const std::vector<std::string> reasonOnlyTrace{
        "rule-evaluation:check reason=check branch",
        "rule-evaluation:value reason=value branch"};
    yaha::automation_trace_format::appendExplainTraceEntries(&traceEntries, reasonOnlyTrace, "");
    REQUIRE(traceEntries.size() >= 7U);
    CHECK(traceEntries[traceEntries.size() - 2U] == "debug:explain check: check branch");
    CHECK(traceEntries.back() == "debug:explain value: value branch");

    std::string richText{"line\\value\t\"quoted\"\r"};
    richText.push_back('\b');
    richText.push_back('\f');
    richText.push_back('\n');
    richText.push_back(static_cast<char>(0x01));

    yaha::Message traceMessage{"$MONITOR/automation/rules/demo/trace", richText};
    traceMessage.addReason("first\\reason\n");
    traceMessage.addReason("second\treason\r");

    const std::string rawPayload = yaha::automation_trace_format::buildTraceRawPayload(traceMessage);
    CHECK(rawPayload.find("line\\\\value") != std::string::npos);
    CHECK(rawPayload.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(rawPayload.find("first\\\\reason\\n") != std::string::npos);
    CHECK(rawPayload.find("second\\treason\\r") != std::string::npos);
    CHECK(rawPayload.find("\\u0001") != std::string::npos);
    CHECK(rawPayload.find("\\b") != std::string::npos);
    CHECK(rawPayload.find("\\f") != std::string::npos);

    yaha::Message numericTraceMessage{"$MONITOR/automation/rules/demo/trace", k_numeric_trace_value};
    numericTraceMessage.addReason("numeric");
    const std::string numericRawPayload = yaha::automation_trace_format::buildTraceRawPayload(numericTraceMessage);
    CHECK(numericRawPayload.find("\"value\":3.500000") != std::string::npos);
}
