#include <catch2/catch_test_macros.hpp>

#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace yaha;
using namespace yaha::http_mqtt_ops_internal;

constexpr int k_http_status_ok{200};
constexpr int k_http_status_no_content{204};
constexpr std::uint16_t k_packet_id_expected{17};
constexpr std::uint16_t k_packet_id_mismatch{18};

HttpMqttResult makeResult(int statusCode, HttpMqttHeaders headers, std::string payload = "") {
    return HttpMqttResult{
        .statusCode = statusCode,
        .headers = std::move(headers),
        .payload = std::move(payload),
    };
}

template <typename Callable>
void requireRuntimeErrorMessage(Callable&& callable, const std::string& expectedMessage) {
    try {
        callable();
        FAIL("expected runtime_error");
    } catch (const std::runtime_error& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()} == expectedMessage);
    }
}

} // namespace

TEST_CASE("helpers_trim_escape_and_value_serialization_cover_special_cases", "[http_mqtt_interface]") {
    REQUIRE(escapeJsonString("a\\b\n\r\t\"") == "a\\\\b\\n\\r\\t\\\"");

    const Value numericValue{12.5};
    REQUIRE(messageValueToJson(numericValue).find("12.5") != std::string::npos);
}

TEST_CASE("helpers_parse_json_value_token_covers_numeric_and_invalid", "[http_mqtt_interface]") {
    const auto numberValue = parseJsonValueToken(" -42.75 ");
    REQUIRE(numberValue.has_value());
    REQUIRE(std::holds_alternative<double>(*numberValue));

    const auto invalidNumber = parseJsonValueToken("12abc");
    REQUIRE_FALSE(invalidNumber.has_value());
}

TEST_CASE("helpers_reason_array_parser_handles_empty_and_invalid_inputs", "[http_mqtt_interface]") {
    const auto emptyArray = parseReasonArray("[]");
    REQUIRE(emptyArray.has_value());
    REQUIRE(emptyArray->empty());

    const auto missingMessage = parseReasonArray(R"([{"timestamp":"2025-01-01T00:00:00Z"}])");
    REQUIRE_FALSE(missingMessage.has_value());

    const auto malformedArray = parseReasonArray(R"([{"message":"m","timestamp":"t"} trailing])");
    REQUIRE_FALSE(malformedArray.has_value());
}

TEST_CASE("helpers_extract_raw_token_handles_nested_object_and_array_and_unclosed_json", "[http_mqtt_interface]") {
    const auto nestedToken = extractRawToken(
        R"({"value":{"inner":"x"},"reason":[{"message":"m","timestamp":"t"}]})",
        "value");
    REQUIRE(nestedToken.has_value());
    REQUIRE(nestedToken->find("\"inner\"") != std::string::npos);

    const auto arrayToken = extractRawToken(
        R"({"value":[1,2,3],"token":"abc"})",
        "value");
    REQUIRE(arrayToken.has_value());
    REQUIRE(*arrayToken == "[1,2,3]");

    const auto unclosed = extractRawToken(R"json({"value":{"inner":1)json", "value");
    REQUIRE_FALSE(unclosed.has_value());
}

TEST_CASE("helpers_qos_retain_and_topic_serialization_cover_remaining_branches", "[http_mqtt_interface]") {
    REQUIRE_FALSE(parseQosField("-1").has_value());
    REQUIRE(parseRetainField("false").has_value());
    REQUIRE(*parseRetainField("false") == false);

    const HttpMqttTopics topics{
        {"a/b", Qos::AtMostOnce},
        {"c/d", Qos::AtLeastOnce},
    };
    const std::string serialized = serializeTopics(topics);
    REQUIRE(serialized.find(',') != std::string::npos);
}

TEST_CASE("helpers_integer_array_parser_throws_for_non_integer_tokens", "[http_mqtt_interface]") {
    requireRuntimeErrorMessage(
        []() {
            (void)parseIntegerArrayPayload("[1, \"x\", 3]");
        },
        "result payload contains non-integer array value");

    const auto parsed = parseIntegerArrayPayload("[1, 2, 3]");
    REQUIRE(parsed == std::vector<int>{1, 2, 3});
}

TEST_CASE("helpers_result_validators_cover_error_paths", "[http_mqtt_interface]") {
    const HttpMqttResult validResult = makeResult(
        k_http_status_ok,
        {
            {"content-type", "application/json; charset=UTF-8"},
            {"packet", "suback"},
            {"packetid", std::to_string(k_packet_id_expected)},
        });

    REQUIRE_NOTHROW(validateStatusCode(validResult, k_http_status_ok, "ctx"));
    REQUIRE_NOTHROW(validateContentTypeJson(validResult, "ctx"));
    REQUIRE_NOTHROW(validateHeaderEquals(validResult, "packet", "suback", "ctx"));
    REQUIRE_NOTHROW(validatePacketIdMatch(validResult, k_packet_id_expected, "ctx"));
    REQUIRE_NOTHROW(validatePacketIdMatch(validResult, std::nullopt, "ctx"));

    requireRuntimeErrorMessage(
        [&validResult]() {
            validateStatusCode(validResult, k_http_status_no_content, "ctx");
        },
        "ctx: invalid status 200 expected 204");
    requireRuntimeErrorMessage(
        []() {
            validateContentTypeJson(makeResult(k_http_status_ok, {{"content-type", "text/plain"}}), "ctx");
        },
        "ctx: invalid content-type header");
    requireRuntimeErrorMessage(
        [&validResult]() {
            validateHeaderEquals(validResult, "packet", "connack", "ctx");
        },
        "ctx: invalid header 'packet' value 'suback' expected 'connack'");
    requireRuntimeErrorMessage(
        [&validResult]() {
            validatePacketIdMatch(validResult, k_packet_id_mismatch, "ctx");
        },
        "ctx: packetid mismatch");
}
