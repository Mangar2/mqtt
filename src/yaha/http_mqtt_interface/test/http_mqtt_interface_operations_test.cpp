#include <catch2/catch_test_macros.hpp>

#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"

#include <stdexcept>
#include <string>

namespace {

constexpr int k_statusNoContent{204};

[[nodiscard]] yaha::HttpMqttResult makeResult(
    const int statusCode,
    yaha::HttpMqttHeaders headersInput,
    std::string payloadText) {
    return yaha::HttpMqttResult{
        .statusCode = statusCode,
        .headers = std::move(headersInput),
        .payload = std::move(payloadText)};
}

} // namespace

TEST_CASE("connect_v1_request_sets_headers_and_keepalive_default", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttConnectOptions options{
        .qos = std::nullopt,
        .clientId = std::string{"test-client"},
        .version = std::nullopt,
        .host = std::nullopt,
        .port = std::nullopt,
        .clean = true,
        .keepAlive = std::nullopt,
        .password = std::nullopt,
        .user = std::nullopt,
        .will = std::nullopt};

    const yaha::HttpMqttRequestData requestData = interfaces.connect("1.0", options);

    REQUIRE(requestData.headers.at("version") == "1.0");
    REQUIRE(requestData.payload.find("\"keepAlive\":0") != std::string::npos);
}

TEST_CASE("connect_v1_result_check_accepts_valid_connack", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttConnectOptions options{
        .qos = std::nullopt,
        .clientId = std::string{"cid"},
        .version = std::nullopt,
        .host = std::nullopt,
        .port = std::nullopt,
        .clean = true,
        .keepAlive = std::nullopt,
        .password = std::nullopt,
        .user = std::nullopt,
        .will = std::nullopt};

    const yaha::HttpMqttRequestData requestData = interfaces.connect("1.0", options);
    const yaha::HttpMqttResult result = makeResult(
        200,
        { {"content-type", "application/json; charset=UTF-8"}, {"packet", "connack"} },
        "{\"present\":1,\"token\":{\"send\":\"send-token\",\"receive\":\"recv-token\"}}"
    );

    REQUIRE_NOTHROW(requestData.resultCheck(result));
}

TEST_CASE("connect_v1_result_check_throws_for_mqtt_error_code", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttRequestData requestData = interfaces.connect(
        "1.0",
        yaha::HttpMqttConnectOptions{
            .qos = std::nullopt,
            .clientId = std::string{"cid"},
            .version = std::nullopt,
            .host = std::nullopt,
            .port = std::nullopt,
            .clean = true,
            .keepAlive = std::nullopt,
            .password = std::nullopt,
            .user = std::nullopt,
            .will = std::nullopt});

    const yaha::HttpMqttResult result = makeResult(
        200,
        { {"content-type", "application/json; charset=UTF-8"}, {"packet", "connack"} },
        "{\"present\":0,\"mqttcode\":4,\"token\":{\"send\":\"send\",\"receive\":\"recv\"}}"
    );

    try {
        requestData.resultCheck(result);
        FAIL("expected mapped mqtt connect error");
    } catch (const std::runtime_error& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()} == "mqtt connect error: bad username or password");
    }
}

TEST_CASE("disconnect_v1_request_and_on_disconnect_response", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttRequestData requestData = interfaces.disconnect("1.0", "client-a");
    const yaha::HttpMqttResult responseData = interfaces.onDisconnect({{"version", "1.0"}});

    REQUIRE(requestData.headers.at("version") == "1.0");
    REQUIRE(requestData.payload.find("client-a") != std::string::npos);

    REQUIRE(responseData.statusCode == 204);
    REQUIRE(responseData.payload.empty());
    REQUIRE(responseData.headers.at("version") == "1.0");
}

TEST_CASE("publish_v1_request_and_result_check_qos1", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    yaha::Message message{"topic/demo", std::string{"value"}, yaha::Qos::AtLeastOnce, false};
    const yaha::HttpMqttPublishOptions options{
        .token = "token-a",
        .message = message,
        .dup = false,
        .packetId = 7U};

    const yaha::HttpMqttRequestData requestData = interfaces.publish("1.0", options);

    REQUIRE(requestData.headers.at("qos") == "1");
    REQUIRE(requestData.headers.at("packetid") == "7");

    const yaha::HttpMqttResult response = makeResult(
        204,
        { {"packet", "puback"}, {"packetid", "7"} },
        ""
    );

    REQUIRE_NOTHROW(requestData.resultCheck(response));
}

TEST_CASE("on_publish_v1_maps_ack_headers_by_qos", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();

    const yaha::HttpMqttResult qosOneResult = interfaces.onPublish(
        {{"version", "1.0"}, {"qos", "1"}, {"retain", "0"}, {"packetid", "8"}}
    );
    const yaha::HttpMqttResult qosTwoResult = interfaces.onPublish(
        {{"version", "1.0"}, {"qos", "2"}, {"retain", "1"}, {"packetid", "9"}}
    );

    REQUIRE(qosOneResult.headers.at("packet") == "puback");
    REQUIRE(qosTwoResult.headers.at("packet") == "pubrec");
}

TEST_CASE("pubrel_v1_request_result_and_response", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttRequestData requestData = interfaces.pubrel(
        "1.0",
        yaha::HttpMqttPubrelOptions{.token = "token-r", .packetId = 33U}
    );

    const yaha::HttpMqttResult checkResult = makeResult(
        204,
        {{"packet", "pubcomp"}, {"packetid", "33"}},
        ""
    );
    REQUIRE_NOTHROW(requestData.resultCheck(checkResult));

    const yaha::HttpMqttResult responseData = interfaces.onPubrel(
        {{"version", "1.0"}, {"packetid", "33"}}
    );

    REQUIRE(responseData.statusCode == 204);
    REQUIRE(responseData.headers.at("packet") == "pubcomp");
    REQUIRE(responseData.headers.at("packetid") == "33");
}

TEST_CASE("subscribe_v1_request_result_and_response", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttTopics topicsInput{{"alpha/#", yaha::Qos::AtLeastOnce}};

    const yaha::HttpMqttRequestData requestData = interfaces.subscribe("1.0", topicsInput, "client-sub", 41U);

    const yaha::HttpMqttResult checkResult = makeResult(
        200,
        { {"content-type", "application/json; charset=UTF-8"}, {"packet", "suback"}, {"packetid", "41"} },
        "{\"qos\":[1]}"
    );

    REQUIRE_NOTHROW(requestData.resultCheck(checkResult));

    const yaha::HttpMqttResult responseData = interfaces.onSubscribe(
        {{"version", "1.0"}, {"packetid", "41"}},
        yaha::HttpMqttSubscribeResult{1U}
    );

    REQUIRE(responseData.statusCode == 200);
    REQUIRE(responseData.headers.at("packet") == "suback");
    REQUIRE(responseData.payload == "{\"qos\":[1]}");
}

TEST_CASE("unsubscribe_v1_accepts_204_empty_payload", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttTopics topicsInput{{"alpha/#", yaha::Qos::AtLeastOnce}};

    const yaha::HttpMqttRequestData requestData = interfaces.unsubscribe("1.0", topicsInput, "client-unsub", 51U);
    const yaha::HttpMqttResult checkResult = makeResult(
        204,
        { {"content-type", "application/json; charset=UTF-8"}, {"packet", "unsuback"}, {"packetid", "51"} },
        ""
    );

    REQUIRE_NOTHROW(requestData.resultCheck(checkResult));
}

TEST_CASE("unsubscribe_v1_result_and_response_with_codes", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttTopics topicsInput{{"beta/#", yaha::Qos::AtLeastOnce}};

    const yaha::HttpMqttRequestData requestData = interfaces.unsubscribe("1.0", topicsInput, "client-unsub", 52U);
    const yaha::HttpMqttResult checkResult = makeResult(
        200,
        { {"content-type", "application/json; charset=UTF-8"}, {"packet", "unsuback"}, {"packetid", "52"} },
        "[0,17]"
    );

    REQUIRE_NOTHROW(requestData.resultCheck(checkResult));

    const yaha::HttpMqttResult responseData = interfaces.onUnsubscribe(
        {{"version", "1.0"}, {"packetid", "52"}},
        yaha::HttpMqttUnsubscribeResult{0U, 17U}
    );

    REQUIRE(responseData.statusCode == 200);
    REQUIRE(responseData.headers.at("packet") == "unsuback");
    REQUIRE(responseData.payload == "[0,17]");
}

TEST_CASE("compat_publish_post_form_maps_to_publish_v1_defaults", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttPublishCompatibilityRequest requestInput{
        .method = "POST",
        .endpoint = "/publish",
        .headers = {},
        .fields = {{"topic", "sensor%2Ftemp"}, {"value", "42"}},
        .body = "",
        .token = "token-compat"};

    yaha::HttpMqttRequestData capturedRequest{};
    const yaha::HttpMqttResult response = yaha::handlePublishCompatibilityRequest(
        interfaces,
        requestInput,
        yaha::HttpMqttPublishCompatibilityConfig{},
        [&](const yaha::HttpMqttRequestData& mappedRequest) {
            capturedRequest = mappedRequest;
            return makeResult(k_statusNoContent, {{"content-type", "application/json; charset=UTF-8"}}, "");
        });

    REQUIRE(response.statusCode == 204);
    REQUIRE(capturedRequest.headers.at("qos") == "1");
    REQUIRE(capturedRequest.headers.at("retain") == "0");
    REQUIRE(capturedRequest.payload.find("\"topic\":\"sensor/temp\"") != std::string::npos);
    REQUIRE(capturedRequest.payload.find("\"message\":\"Request by browser\"") != std::string::npos);
}

TEST_CASE("compat_publish_falls_back_to_json_body_when_topic_missing", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttPublishCompatibilityRequest requestInput{
        .method = "POST",
        .endpoint = "/publish",
        .headers = {{"content-type", "application/json"}},
        .fields = {},
        .body = "{\"topic\":\"alpha%2fbeta\",\"value\":\"payload\"}",
        .token = "token-body"};

    yaha::HttpMqttRequestData capturedRequest{};
    const yaha::HttpMqttResult response = yaha::handlePublishCompatibilityRequest(
        interfaces,
        requestInput,
        yaha::HttpMqttPublishCompatibilityConfig{},
        [&](const yaha::HttpMqttRequestData& mappedRequest) {
            capturedRequest = mappedRequest;
            return makeResult(k_statusNoContent, {{"content-type", "application/json; charset=UTF-8"}}, "");
        });

    REQUIRE(response.statusCode == 204);
    REQUIRE(capturedRequest.payload.find("\"topic\":\"alpha/beta\"") != std::string::npos);
    REQUIRE(capturedRequest.payload.find("\"value\":\"payload\"") != std::string::npos);
}

TEST_CASE("compat_publish_php_alias_disabled_returns_405", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttPublishCompatibilityRequest requestInput{
        .method = "POST",
        .endpoint = "/publish.php",
        .headers = {},
        .fields = {{"topic", "alpha"}},
        .body = "",
        .token = ""};

    const yaha::HttpMqttPublishCompatibilityConfig configInput{
        .enablePublishPhpAlias = false,
        .responseMode = yaha::HttpMqttPublishCompatibilityResponseMode::Native};

    bool forwarded = false;
    const yaha::HttpMqttResult response = yaha::handlePublishCompatibilityRequest(
        interfaces,
        requestInput,
        configInput,
        [&](const yaha::HttpMqttRequestData&) {
            forwarded = true;
            return makeResult(k_statusNoContent, {}, "");
        });

    REQUIRE(response.statusCode == 405);
    REQUIRE(forwarded == false);
}

TEST_CASE("compat_publish_legacy_mode_wraps_downstream_payload", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttPublishCompatibilityRequest requestInput{
        .method = "POST",
        .endpoint = "/publish",
        .headers = {},
        .fields = {{"topic", "legacy/topic"}},
        .body = "",
        .token = "token-legacy"};

    const yaha::HttpMqttPublishCompatibilityConfig configInput{
        .enablePublishPhpAlias = true,
        .responseMode = yaha::HttpMqttPublishCompatibilityResponseMode::LegacyPhp};

    const yaha::HttpMqttResult response = yaha::handlePublishCompatibilityRequest(
        interfaces,
        requestInput,
        configInput,
        [&](const yaha::HttpMqttRequestData&) {
            return makeResult(k_statusNoContent, {{"content-type", "application/json; charset=UTF-8"}}, "{\"ok\":1}");
        });

    REQUIRE(response.statusCode == 200);
    REQUIRE(response.payload == "\"{\\\"ok\\\":1}\"");
}

TEST_CASE("compat_publish_invalid_json_returns_400", "[http_mqtt_interface]") {
    const yaha::HttpMqttInterfaces interfaces = yaha::makeHttpMqttInterfacesV1();
    const yaha::HttpMqttPublishCompatibilityRequest requestInput{
        .method = "POST",
        .endpoint = "/publish",
        .headers = {{"content-type", "application/json"}},
        .fields = {},
        .body = "{\"topic\":",
        .token = ""};

    bool forwarded = false;
    const yaha::HttpMqttResult response = yaha::handlePublishCompatibilityRequest(
        interfaces,
        requestInput,
        yaha::HttpMqttPublishCompatibilityConfig{},
        [&](const yaha::HttpMqttRequestData&) {
            forwarded = true;
            return makeResult(k_statusNoContent, {}, "");
        });

    REQUIRE(response.statusCode == 400);
    REQUIRE(forwarded == false);
}
