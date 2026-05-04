#include <catch2/catch_test_macros.hpp>

#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

#include <stdexcept>
#include <string>

namespace {

constexpr int k_okStatusCode{200};

[[nodiscard]] yaha::HttpMqttResult buildResultWithPayload(std::string payloadText) {
    return yaha::HttpMqttResult{
    .statusCode = k_okStatusCode,
        .headers = yaha::makeStandardJsonHeaders(),
        .payload = std::move(payloadText)};
}

} // namespace

TEST_CASE("dispatcher_get_version_defaults_to_0_0", "[http_mqtt_interface]") {
    const yaha::HttpMqttPublishResponseHandlerMap onPublishHandlers{
        {"0.0", [](const yaha::HttpMqttHeaders&) {
             return buildResultWithPayload("fallback");
         }}};
    const yaha::HttpMqttHeaders headersInput{};

    REQUIRE(yaha::getVersion(headersInput, onPublishHandlers) == "0.0");
}

TEST_CASE("dispatcher_get_version_throws_for_undefined_version", "[http_mqtt_interface]") {
    const yaha::HttpMqttPublishResponseHandlerMap onPublishHandlers{
        {"1.0", [](const yaha::HttpMqttHeaders&) {
             return buildResultWithPayload("v1");
         }}};
    const yaha::HttpMqttHeaders headersInput{{"version", "9.9"}};

    try {
        const auto versionText = yaha::getVersion(headersInput, onPublishHandlers);
        (void)versionText;
        FAIL("expected undefined version exception");
    } catch (const std::runtime_error& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()} == "undefined version 9.9");
    }
}

TEST_CASE("interfaces_on_publish_dispatches_by_resolved_version", "[http_mqtt_interface]") {
    yaha::HttpMqttInterfaceHandlerRegistry registry{};
    registry.publishResponses["1.0"] = [](const yaha::HttpMqttHeaders&) {
        return buildResultWithPayload("publish-v1");
    };

    const yaha::HttpMqttInterfaces interfaces{std::move(registry)};
    const yaha::HttpMqttResult result = interfaces.onPublish({{"version", "1.0"}});

    REQUIRE(result.payload == "publish-v1");
}

TEST_CASE("interfaces_on_publish_uses_0_0_fallback", "[http_mqtt_interface]") {
    yaha::HttpMqttInterfaceHandlerRegistry registry{};
    registry.publishResponses["0.0"] = [](const yaha::HttpMqttHeaders&) {
        return buildResultWithPayload("publish-v0");
    };

    const yaha::HttpMqttInterfaces interfaces{std::move(registry)};
    const yaha::HttpMqttResult result = interfaces.onPublish({});

    REQUIRE(result.payload == "publish-v0");
}

TEST_CASE("interfaces_publish_request_dispatches_by_explicit_version", "[http_mqtt_interface]") {
    yaha::HttpMqttInterfaceHandlerRegistry registry{};
    registry.publishRequests["1.0"] = [](const yaha::HttpMqttPublishOptions& options) {
        yaha::HttpMqttRequestData requestData{};
        requestData.headers = yaha::makeStandardJsonHeaders();
        requestData.payload = options.token;
        return requestData;
    };

    const yaha::HttpMqttInterfaces interfaces{std::move(registry)};
    const yaha::HttpMqttPublishOptions options{
        .token = "token-1",
        .message = yaha::Message{"topic/value", std::string{"payload"}}};

    const yaha::HttpMqttRequestData requestData = interfaces.publish("1.0", options);
    REQUIRE(requestData.payload == "token-1");
}

TEST_CASE("interfaces_on_connect_and_on_disconnect_use_dispatcher_version", "[http_mqtt_interface]") {
    yaha::HttpMqttInterfaceHandlerRegistry registry{};
    registry.publishResponses["1.0"] = [](const yaha::HttpMqttHeaders&) {
        return buildResultWithPayload("publish-v1");
    };
    registry.connectResponses["1.0"] = [](const yaha::HttpMqttConnectResult&) {
        return buildResultWithPayload("connect-v1");
    };
    registry.disconnectResponses["1.0"] = []() {
        return buildResultWithPayload("disconnect-v1");
    };

    const yaha::HttpMqttInterfaces interfaces{std::move(registry)};
    const yaha::HttpMqttConnectResult connectPayload{
        .mqttCode = std::nullopt,
        .present = 1U,
        .token = yaha::HttpMqttConnectTokens{.send = "send", .receive = "receive"}};

    const yaha::HttpMqttResult connectResult = interfaces.onConnect({{"version", "1.0"}}, connectPayload);
    const yaha::HttpMqttResult disconnectResult = interfaces.onDisconnect({{"version", "1.0"}});

    REQUIRE(connectResult.payload == "connect-v1");
    REQUIRE(disconnectResult.payload == "disconnect-v1");
}
