#include <catch2/catch_test_macros.hpp>

#include "yaha/message/message_log_service.h"

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path writeTempIniFile(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_message_log_service_test_" + std::to_string(stamp) + ".ini");

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

[[nodiscard]] yaha::IniDocument loadIniDocument(const std::string& content) {
    const auto path = writeTempIniFile(content);
    const auto document = yaha::IniDocument::loadFromFile(path);
    std::filesystem::remove(path);
    return document;
}

} // namespace

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

TEST_CASE("Message log service ini loader keeps defaults when keys are missing", "[message][message_log_service]") {
    const yaha::IniDocument document = loadIniDocument("");

    yaha::MessageLogConfig config{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = false,
    };
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadMessageLogConfigFromIni(
        document,
        yaha::MessageLogIniKeys{
            .incomingEnabled = yaha::MessageLogIniBoolKey{.section = "automation", .key = "logIncomingMessages"},
            .outgoingEnabled = yaha::MessageLogIniBoolKey{.section = "automation", .key = "logOutgoingMessages"},
            .includeReasonChain = yaha::MessageLogIniBoolKey{.section = "messagestore", .key = "logReason"},
        },
        config,
        errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.enableIncoming);
    REQUIRE_FALSE(config.enableOutgoing);
    REQUIRE_FALSE(config.includeReasonChain);
}

TEST_CASE("Message log service ini loader parses plural legacy keys", "[message][message_log_service]") {
    const yaha::IniDocument document = loadIniDocument(
        "[automation]\n"
        "logIncomingMessages=true\n"
        "logOutgoingMessages=true\n"
        "\n"
        "[messagestore]\n"
        "logReason=false\n");

    yaha::MessageLogConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadMessageLogConfigFromIni(
        document,
        yaha::MessageLogIniKeys{
            .incomingEnabled = yaha::MessageLogIniBoolKey{.section = "automation", .key = "logIncomingMessages"},
            .outgoingEnabled = yaha::MessageLogIniBoolKey{.section = "automation", .key = "logOutgoingMessages"},
            .includeReasonChain = yaha::MessageLogIniBoolKey{.section = "messagestore", .key = "logReason"},
        },
        config,
        errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.enableIncoming);
    REQUIRE(config.enableOutgoing);
    REQUIRE_FALSE(config.includeReasonChain);
}

TEST_CASE("Message log service ini loader parses singular legacy monitoring keys", "[message][message_log_service]") {
    const yaha::IniDocument document = loadIniDocument(
        "[monitoring]\n"
        "logIncomingMessage=false\n"
        "logOutgoingMessage=true\n");

    yaha::MessageLogConfig config{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadMessageLogConfigFromIni(
        document,
        yaha::MessageLogIniKeys{
            .incomingEnabled = yaha::MessageLogIniBoolKey{.section = "monitoring", .key = "logIncomingMessage"},
            .outgoingEnabled = yaha::MessageLogIniBoolKey{.section = "monitoring", .key = "logOutgoingMessage"},
            .includeReasonChain = std::nullopt,
        },
        config,
        errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(config.enableIncoming);
    REQUIRE(config.enableOutgoing);
    REQUIRE(config.includeReasonChain);
}

TEST_CASE("Message log service ini loader falls back on invalid bool key", "[message][message_log_service]") {
    const yaha::IniDocument document = loadIniDocument(
        "[automation]\n"
        "logIncomingMessages=maybe\n");

    yaha::MessageLogConfig config{};
    std::string errorMessage{};

    const bool loaded = yaha::tryLoadMessageLogConfigFromIni(
        document,
        yaha::MessageLogIniKeys{
            .incomingEnabled = yaha::MessageLogIniBoolKey{.section = "automation", .key = "logIncomingMessages"},
            .outgoingEnabled = std::nullopt,
            .includeReasonChain = std::nullopt,
        },
        config,
        errorMessage);

    REQUIRE(loaded);
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(config.enableIncoming);
}
