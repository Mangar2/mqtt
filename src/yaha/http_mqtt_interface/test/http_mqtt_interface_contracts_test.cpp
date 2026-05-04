#include <catch2/catch_test_macros.hpp>

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"

#include <stdexcept>
#include <string>

TEST_CASE("json_headers_match_phase1_defaults", "[http_mqtt_interface]") {
    const yaha::HttpMqttHeaders headersMap = yaha::makeStandardJsonHeaders();

    REQUIRE(headersMap.at("content-type") == "application/json; charset=UTF-8");
    REQUIRE(headersMap.at("accept") == "application/json,text/plain");
    REQUIRE(headersMap.at("accept-charset") == "UTF-8");
}

TEST_CASE("text_headers_match_phase1_defaults", "[http_mqtt_interface]") {
    const yaha::HttpMqttHeaders headersMap = yaha::makeStandardTextHeaders();

    REQUIRE(headersMap.at("content-type") == "text/plain; charset=UTF-8");
    REQUIRE(headersMap.at("accept") == "application/json,text/plain");
    REQUIRE(headersMap.at("accept-charset") == "UTF-8");
}

TEST_CASE("header_lookup_is_case_insensitive", "[http_mqtt_interface]") {
    const yaha::HttpMqttHeaders headersMap{
        {"Content-Type", "application/json; charset=UTF-8"},
        {"PACKETID", "42"}};

    const auto contentType = yaha::tryReadHeaderValue(headersMap, "content-type");
    REQUIRE(contentType.has_value());
    REQUIRE(*contentType == "application/json; charset=UTF-8");

    const auto packetIdentifier = yaha::readPacketIdHeader(headersMap);
    REQUIRE(packetIdentifier.has_value());
    REQUIRE(*packetIdentifier == 42U);
}

TEST_CASE("require_header_throws_for_missing_key", "[http_mqtt_interface]") {
    const yaha::HttpMqttHeaders headersMap{};

    try {
        const auto valueText = yaha::requireHeaderValue(headersMap, "packet");
        (void)valueText;
        FAIL("expected missing required header exception");
    } catch (const std::runtime_error& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()} == "missing required header 'packet'");
    }
}

TEST_CASE("packet_id_parser_accepts_valid_decimal", "[http_mqtt_interface]") {
    const auto packetIdentifier = yaha::parsePacketId("65535");

    REQUIRE(packetIdentifier.has_value());
    REQUIRE(*packetIdentifier == 65535U);
}

TEST_CASE("packet_id_parser_rejects_invalid_text", "[http_mqtt_interface]") {
    REQUIRE_FALSE(yaha::parsePacketId("-1").has_value());
    REQUIRE_FALSE(yaha::parsePacketId("65a").has_value());
    REQUIRE_FALSE(yaha::parsePacketId("70000").has_value());
}

TEST_CASE("resolve_version_uses_default_when_absent", "[http_mqtt_interface]") {
    const yaha::HttpMqttHeaders headersMap{{"packet", "puback"}};

    REQUIRE(yaha::resolveVersion(headersMap, "0.0") == "0.0");
}

TEST_CASE("require_json_object_payload_accepts_trimmed_object", "[http_mqtt_interface]") {
    REQUIRE_NOTHROW(yaha::requireJsonObjectPayload("  {\"field\":1}  ", "publish"));
}

TEST_CASE("require_json_object_payload_rejects_non_object", "[http_mqtt_interface]") {
    try {
        yaha::requireJsonObjectPayload("[1,2]", "publish");
        FAIL("expected json object validation exception");
    } catch (const std::runtime_error& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()} == "publish: payload must be a JSON object");
    }
}
