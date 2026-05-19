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

TEST_CASE("Message log formatter emits deterministic required field order", "[message][message_log_formatter]") {
    yaha::Message message{"topic/order", std::string{"on"}, yaha::Qos::AtLeastOnce, false, true};
    message.addReason("rule triggered", "2026-05-19T11:00:00Z");

    const std::string line = yaha::formatMessageLogLine(
        "automation_client",
        yaha::MessageLogDirection::Outgoing,
        message,
        true);

    const std::size_t componentPos = findOrFail(line, "component=");
    const std::size_t directionPos = findOrFail(line, "direction=");
    const std::size_t topicPos = findOrFail(line, "topic=");
    const std::size_t valuePos = findOrFail(line, "value=");
    const std::size_t qosPos = findOrFail(line, "qos=");
    const std::size_t retainPos = findOrFail(line, "retain=");
    const std::size_t dupPos = findOrFail(line, "dup=");
    const std::size_t reasonPos = findOrFail(line, "reason=");

    REQUIRE(componentPos < directionPos);
    REQUIRE(directionPos < topicPos);
    REQUIRE(topicPos < valuePos);
    REQUIRE(valuePos < qosPos);
    REQUIRE(qosPos < retainPos);
    REQUIRE(retainPos < dupPos);
    REQUIRE(dupPos < reasonPos);
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
