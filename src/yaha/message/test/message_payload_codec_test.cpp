#include <catch2/catch_test_macros.hpp>

#include "yaha/message/message.h"
#include "yaha/message/message_payload_codec.h"

#include <optional>
#include <string>

namespace {

constexpr double k_expected_numeric_value{77.5};

} // namespace

TEST_CASE("Payload codec escapes JSON control characters", "[message][payload_codec]") {
    const std::string escaped = yaha::escapeJsonString("a\"b\\c\n\t");

    REQUIRE(escaped == "a\\\"b\\\\c\\n\\t");
}

TEST_CASE("Payload codec serializes reasons oldest first", "[message][payload_codec]") {
    yaha::Message messageValue{"topic/order", std::string{"on"}};
    messageValue.addReason("old", "2026-01-01T00:00:00Z");
    messageValue.addReason("new", "2026-01-02T00:00:00Z");

    const std::string reasonJson = yaha::serializeReasonArrayOldestFirst(messageValue.reason());
    const std::size_t oldPosition = reasonJson.find(R"("message":"old")");
    const std::size_t newPosition = reasonJson.find(R"("message":"new")");

    REQUIRE(oldPosition != std::string::npos);
    REQUIRE(newPosition != std::string::npos);
    REQUIRE(oldPosition < newPosition);
}

TEST_CASE("Payload codec buildEnvelopePayload emits canonical envelope", "[message][payload_codec]") {
    yaha::Message messageValue{"topic/build", std::string{"v"}, yaha::Qos::AtLeastOnce, false, true};
    messageValue.addReason("set", "2026-05-19T10:00:00Z");

    const std::string payload = yaha::buildEnvelopePayload(messageValue);

    REQUIRE(payload.find("{\"message\":{") == 0U);
    REQUIRE(payload.find("\"topic\":\"topic/build\"") != std::string::npos);
    REQUIRE(payload.find("\"value\":\"v\"") != std::string::npos);
    REQUIRE(payload.find("\"reason\":[") != std::string::npos);
}

TEST_CASE("Payload codec parseEnvelopePayload parses numeric value", "[message][payload_codec]") {
    const std::string payload =
        R"({"message":{"topic":"topic/numeric","value":77.5}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        payload,
        "topic/numeric",
        yaha::Qos::AtLeastOnce,
        false,
        false);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<double>(parsed->value()));
    REQUIRE(std::get<double>(parsed->value()) == k_expected_numeric_value);
}

TEST_CASE("Payload codec parseEnvelopePayload coerces bool token to string", "[message][payload_codec]") {
    const std::string payload =
        R"({"message":{"topic":"topic/bool","value":true}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        payload,
        "topic/bool",
        yaha::Qos::AtMostOnce,
        false,
        false);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<std::string>(parsed->value()));
    REQUIRE(std::get<std::string>(parsed->value()) == "true");
}

TEST_CASE("Payload codec parseEnvelopePayload rejects malformed envelope", "[message][payload_codec]") {
    const std::string payload =
        R"({"message" {"topic":"topic/malformed","value":"x"}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        payload,
        "topic/malformed",
        yaha::Qos::AtMostOnce,
        false,
        false);

    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("Payload codec parseReasonArray rejects missing message field", "[message][payload_codec]") {
    const std::optional<std::vector<yaha::ReasonEntry>> parsed = yaha::parseReasonArray(
        R"([{"timestamp":"2026-01-01T00:00:00Z"}])");

    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("Payload codec validateEnvelopeShape enforces topic and value", "[message][payload_codec]") {
    const std::string validPayload =
        R"({"message":{"topic":"topic/shape","value":"ok"}})";
    const std::string missingTopicPayload =
        R"({"message":{"value":"ok"}})";
    const std::string missingValuePayload =
        R"({"message":{"topic":"topic/shape"}})";

    REQUIRE(yaha::validateEnvelopeShape(validPayload));
    REQUIRE_FALSE(yaha::validateEnvelopeShape(missingTopicPayload));
    REQUIRE_FALSE(yaha::validateEnvelopeShape(missingValuePayload));
}

TEST_CASE("Payload codec envelope matches TS reference ordering", "[message][payload_codec]") {
    yaha::Message messageValue{"topic/ts", std::string{"on"}};
    messageValue.addReason("first", "2026-05-19T10:00:00.000Z");
    messageValue.addReason("second", "2026-05-19T10:00:01.000Z");

    const std::string payload = yaha::buildEnvelopePayload(messageValue);
    const std::size_t firstPosition = payload.find(R"("message":"first")");
    const std::size_t secondPosition = payload.find(R"("message":"second")");

    REQUIRE(firstPosition != std::string::npos);
    REQUIRE(secondPosition != std::string::npos);
    REQUIRE(firstPosition < secondPosition);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Payload codec parse and rebuild keeps TS compatible envelope", "[message][payload_codec]") {
    const std::string sourcePayload =
        R"({"message":{"topic":"topic/ts/parse","value":12.5,"reason":[{"timestamp":"2026-05-19T10:00:00.000Z","message":"alpha\nvalue"},{"timestamp":"2026-05-19T10:00:01.000Z","message":"beta\tvalue"}]}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        sourcePayload,
        "topic/ts/parse",
        yaha::Qos::AtLeastOnce,
        false,
        false);

    REQUIRE(parsed.has_value());
    REQUIRE(yaha::validateEnvelopeShape(sourcePayload));

    const std::string rebuiltPayload = yaha::buildEnvelopePayload(*parsed);
    REQUIRE(yaha::validateEnvelopeShape(rebuiltPayload));
    REQUIRE(rebuiltPayload.find(R"("topic":"topic/ts/parse")") != std::string::npos);
    REQUIRE(rebuiltPayload.find(R"("value":12.500000)") != std::string::npos);

    const std::optional<yaha::Message> reparsed = yaha::parseEnvelopePayload(
        rebuiltPayload,
        "topic/ts/parse",
        yaha::Qos::AtLeastOnce,
        false,
        false);
    REQUIRE(reparsed.has_value());
    REQUIRE(reparsed->reason().size() == 2U);
    REQUIRE(reparsed->reason()[0].message == "beta\tvalue");
    REQUIRE(reparsed->reason()[1].message == "alpha\nvalue");
}

TEST_CASE("Payload codec decodes full JSON escape set", "[message][payload_codec]") {
    const std::string payload =
        R"({"message":{"topic":"topic/escape/full","value":"quote:\" slash:\/ backslash:\\ cr:\r bs:\b ff:\f u-upper:\u0041 u-lower:\u0061"}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        payload,
        "topic/escape/full",
        yaha::Qos::AtLeastOnce,
        false,
        false);

    REQUIRE(parsed.has_value());
    REQUIRE(std::holds_alternative<std::string>(parsed->value()));

    const std::string expected = std::string{"quote:\" slash:/ backslash:\\ cr:"}
        + '\r'
        + std::string{" bs:"}
        + '\b'
        + std::string{" ff:"}
        + '\f'
        + std::string{" u-upper:A u-lower:a"};
    REQUIRE(std::get<std::string>(parsed->value()) == expected);
}

TEST_CASE("Payload codec rejects invalid escaped token", "[message][payload_codec]") {
    const std::string payload =
        R"({"message":{"topic":"topic/escape/invalid","value":"bad\qescape"}})";

    const std::optional<yaha::Message> parsed = yaha::parseEnvelopePayload(
        payload,
        "topic/escape/invalid",
        yaha::Qos::AtMostOnce,
        false,
        false);

    REQUIRE_FALSE(parsed.has_value());
}
