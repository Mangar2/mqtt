#include <catch2/catch_test_macros.hpp>

#include "yaha/message/message_log_formatter.h"

#include <string>

namespace {

[[nodiscard]] std::size_t findOrFail(const std::string& lineText, const std::string& tokenText) {
    const std::size_t position = lineText.find(tokenText);
    REQUIRE(position != std::string::npos);
    return position;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Message log formatter emits deterministic required field order", "[message][message_log_formatter]") {
    yaha::Message message{"topic/order", std::string{"on"}, yaha::Qos::AtLeastOnce, true, true};
    message.addReason("rule triggered", "2026-05-19T11:00:00Z");

    const std::string line = yaha::formatMessageLogLine(
        "automation_client",
        yaha::MessageLogDirection::Outgoing,
        message,
        true);

    const std::size_t componentPos = findOrFail(line, "automation_client");
    const std::size_t arrowPos = findOrFail(line, "->");
    const std::size_t topicPos = findOrFail(line, "topic/order : ");
    const std::size_t valuePos = findOrFail(line, "\"on\"");
    const std::size_t qosPos = findOrFail(line, "qos=1");
    const std::size_t retainPos = findOrFail(line, "retain");
    const std::size_t dupPos = findOrFail(line, "dup");
    const std::size_t reasonPos = findOrFail(line, "reason=");

    REQUIRE(componentPos < arrowPos);
    REQUIRE(arrowPos < topicPos);
    REQUIRE(topicPos < valuePos);
    REQUIRE(valuePos < qosPos);
    REQUIRE(qosPos < retainPos);
    REQUIRE(retainPos < dupPos);
    REQUIRE(dupPos < reasonPos);
}

TEST_CASE("Message log formatter omits false flags and uses incoming arrow", "[message][message_log_formatter]") {
    constexpr double kSensorValue = 5.0;
    yaha::Message message{"topic/flags", kSensorValue, yaha::Qos::AtMostOnce, false, false};

    const std::string line = yaha::formatMessageLogLine(
        "zwave_service",
        yaha::MessageLogDirection::Incoming,
        message,
        true);

    REQUIRE(line.find("<-") != std::string::npos);
    REQUIRE(line.find("retain") == std::string::npos);
    REQUIRE(line.find("dup") == std::string::npos);
    REQUIRE(line.find("component=") == std::string::npos);
    REQUIRE(line.find("direction=") == std::string::npos);
    REQUIRE(line.find("topic=") == std::string::npos);
    REQUIRE(line.find("value=") == std::string::npos);
}

TEST_CASE("Message log formatter escapes control characters deterministically", "[message][message_log_formatter]") {
    yaha::Message message{"topic/escape", std::string{"line\n\"quote\"\\tab\t"}};
    message.addReason("reason\nline\t", "2026-05-19T11:00:01Z");

    const std::string line = yaha::formatMessageLogLine(
        "mqtt_client",
        yaha::MessageLogDirection::Incoming,
        message,
        true);

    REQUIRE(line.find("line\\n\\\"quote\\\"\\\\tab\\t") != std::string::npos);
    REQUIRE(line.find("reason\\nline\\t") != std::string::npos);
}

TEST_CASE("Message log formatter emits full reason chain", "[message][message_log_formatter]") {
    yaha::Message message{"topic/reason", std::string{"value"}};
    message.addReason("older", "2026-05-19T10:59:59Z");
    message.addReason("newer", "2026-05-19T11:00:00Z");

    const std::string line = yaha::formatMessageLogLine(
        "zwave_service",
        yaha::MessageLogDirection::Outgoing,
        message,
        true);

    const std::size_t newerPos = line.find(R"("message":"newer")");
    const std::size_t olderPos = line.find(R"("message":"older")");

    REQUIRE(newerPos != std::string::npos);
    REQUIRE(olderPos != std::string::npos);
    REQUIRE(newerPos < olderPos);
}
