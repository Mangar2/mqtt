#include <catch2/catch_test_macros.hpp>

#include "yaha/message/message_log_service.h"

#include <string>

TEST_CASE("Message log service disables incoming when flag false", "[message][message_log_service]") {
    const yaha::Message message{"house/lamp/state", std::string{"on"}};
    const yaha::MessageLogConfig config{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const auto line = yaha::buildMessageLogLine(
        "value_service",
        yaha::MessageLogDirection::Incoming,
        message,
        config);

    REQUIRE_FALSE(line.has_value());
}

TEST_CASE("Message log service disables outgoing when flag false", "[message][message_log_service]") {
    const yaha::Message message{"house/lamp/state", std::string{"on"}};
    const yaha::MessageLogConfig config{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const auto line = yaha::buildMessageLogLine(
        "value_service",
        yaha::MessageLogDirection::Outgoing,
        message,
        config);

    REQUIRE_FALSE(line.has_value());
}

TEST_CASE("Message log service applies incoming topic filter", "[message][message_log_service]") {
    const yaha::Message message{"house/lamp/state", std::string{"on"}};
    const yaha::MessageLogConfig config{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
        .incomingTopicFilter = std::string{"house/+/set"},
        .outgoingTopicFilter = std::nullopt};

    const auto line = yaha::buildMessageLogLine(
        "value_service",
        yaha::MessageLogDirection::Incoming,
        message,
        config);

    REQUIRE_FALSE(line.has_value());
}

TEST_CASE("Message log service applies outgoing topic filter", "[message][message_log_service]") {
    const yaha::Message message{"house/lamp/state", std::string{"on"}};
    const yaha::MessageLogConfig config{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::string{"house/+/set"}};

    const auto line = yaha::buildMessageLogLine(
        "value_service",
        yaha::MessageLogDirection::Outgoing,
        message,
        config);

    REQUIRE_FALSE(line.has_value());
}

TEST_CASE("Message log service can omit reason chain by config", "[message][message_log_service]") {
    yaha::Message message{"house/lamp/set", std::string{"on"}};
    message.addReason("rule", "2026-05-19T11:20:00Z");

    const yaha::MessageLogConfig config{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = false,
        .incomingTopicFilter = std::nullopt,
        .outgoingTopicFilter = std::nullopt};

    const auto line = yaha::buildMessageLogLine(
        "value_service",
        yaha::MessageLogDirection::Outgoing,
        message,
        config);

    REQUIRE(line.has_value());
    REQUIRE(line->find("reason=[]") != std::string::npos);
    REQUIRE(line->find("\"message\":\"rule\"") == std::string::npos);
}
