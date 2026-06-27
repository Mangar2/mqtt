#include <catch2/catch_test_macros.hpp>

#include "yaha/http_mqtt_interface_client/http_mqtt_session_manager.h"

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint16_t k_default_broker_port{1883U};
constexpr std::uint16_t k_override_broker_port{2883U};
constexpr std::uint32_t k_default_keep_alive_seconds{30U};
constexpr std::uint32_t k_override_keep_alive_seconds{7U};
constexpr int k_unknown_connect_error_marker{42};
constexpr int k_unknown_unsubscribe_error_marker{9};
constexpr int k_unknown_ping_error_marker{7};

struct CapturedConfig {
    std::string brokerHost{};
    std::uint16_t brokerPort{0U};
    std::string clientId{};
    std::chrono::milliseconds keepAlive{};
};

yaha::YahaMqttClient::Config makeBaseConfig() {
    yaha::YahaMqttClient::Config config{};
    config.brokerHost = "base-host";
    config.brokerPort = k_default_broker_port;
    config.clientId = "base-client";
    config.keepAliveInterval = std::chrono::seconds{k_default_keep_alive_seconds};
    return config;
}

} // namespace

TEST_CASE("http_mqtt_session_manager_constructor_without_factory_and_empty_clientid", "[http_mqtt_interface_client]") {
    yaha::HttpMqttSessionManager manager{makeBaseConfig(), {}};

    yaha::HttpMqttSessionConnectRequest request{};
    request.clientId = "";

    yaha::HttpMqttSessionConnectTokens tokens{};
    std::string errorText{};
    const bool isConnected = manager.connect(request, tokens, errorText);

    REQUIRE_FALSE(isConnected);
    REQUIRE(errorText == "clientId is required");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_session_manager_connect_overrides_and_disconnect_via_receive_token", "[http_mqtt_interface_client]") {
    auto captured = std::make_shared<CapturedConfig>();

    yaha::HttpMqttSessionManager manager{
        makeBaseConfig(),
        [captured]() {
            yaha::YahaMqttClient::Transport transport{};
            transport.connect = [captured](const yaha::YahaMqttClient::Config& config) {
                captured->brokerHost = config.brokerHost;
                captured->brokerPort = config.brokerPort;
                captured->clientId = config.clientId;
                captured->keepAlive = config.keepAliveInterval;
                return true;
            };
            transport.disconnect = []() {};
            transport.publish = [](const yaha::Message&) {};
            transport.subscribe = [](const std::string&, const yaha::Qos) { return true; };
            transport.unsubscribe = [](const std::string&) { return true; };
            transport.pollIncoming = []() { return std::optional<yaha::Message>{}; };
            transport.ping = []() {};
            transport.isConnected = []() { return true; };
            return transport;
        }};

    yaha::HttpMqttSessionConnectRequest request{};
    request.clientId = "http-client";
    request.brokerHost = "override-host";
    request.brokerPort = k_override_broker_port;
    request.keepAliveSeconds = k_override_keep_alive_seconds;

    yaha::HttpMqttSessionConnectTokens tokens{};
    std::string errorText{};

    REQUIRE(manager.connect(request, tokens, errorText));
    REQUIRE(errorText.empty());
    REQUIRE(captured->brokerHost == "override-host");
    REQUIRE(captured->brokerPort == k_override_broker_port);
    REQUIRE(captured->clientId == "http-client");
    REQUIRE(captured->keepAlive == std::chrono::seconds{k_override_keep_alive_seconds});

    REQUIRE(manager.hasSession(tokens.sendToken));
    REQUIRE(manager.hasSession(tokens.receiveToken));

    std::string resolvedClientId{};
    REQUIRE(manager.resolveClientIdByToken(tokens.sendToken, resolvedClientId));
    REQUIRE(resolvedClientId == "http-client");
    REQUIRE(manager.resolveClientIdByToken(tokens.receiveToken, resolvedClientId));
    REQUIRE(resolvedClientId == "http-client");
    REQUIRE_FALSE(manager.resolveClientIdByToken("unknown-token", resolvedClientId));

    REQUIRE(manager.disconnect(tokens.receiveToken, errorText));
    REQUIRE_FALSE(manager.hasSession(tokens.sendToken));
    REQUIRE_FALSE(manager.hasSession(tokens.receiveToken));
}

TEST_CASE("http_mqtt_session_manager_connect_failure_paths", "[http_mqtt_interface_client]") {
    auto makeRequest = []() {
        yaha::HttpMqttSessionConnectRequest request{};
        request.clientId = "http-client";
        return request;
    };

    {
        yaha::HttpMqttSessionManager manager{
            makeBaseConfig(),
            []() {
                yaha::YahaMqttClient::Transport transport{};
                transport.connect = [](const yaha::YahaMqttClient::Config&) { return false; };
                return transport;
            }};

        yaha::HttpMqttSessionConnectTokens tokens{};
        std::string errorText{};
        REQUIRE_FALSE(manager.connect(makeRequest(), tokens, errorText));
        REQUIRE(errorText == "broker connect failed");
    }

    {
        yaha::HttpMqttSessionManager manager{
            makeBaseConfig(),
            []() {
                yaha::YahaMqttClient::Transport transport{};
                transport.connect = [](const yaha::YahaMqttClient::Config&) -> bool {
                    throw std::runtime_error{"connect boom"};
                };
                return transport;
            }};

        yaha::HttpMqttSessionConnectTokens tokens{};
        std::string errorText{};
        REQUIRE_FALSE(manager.connect(makeRequest(), tokens, errorText));
        REQUIRE(errorText == "connect boom");
    }

    {
        yaha::HttpMqttSessionManager manager{
            makeBaseConfig(),
            []() {
                yaha::YahaMqttClient::Transport transport{};
                transport.connect = [](const yaha::YahaMqttClient::Config&) -> bool {
                    throw k_unknown_connect_error_marker;
                };
                return transport;
            }};

        yaha::HttpMqttSessionConnectTokens tokens{};
        std::string errorText{};
        REQUIRE_FALSE(manager.connect(makeRequest(), tokens, errorText));
        REQUIRE(errorText == "unknown broker connect failure");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_session_manager_operation_error_paths", "[http_mqtt_interface_client]") {
    yaha::HttpMqttSessionManager manager{
        makeBaseConfig(),
        []() {
            yaha::YahaMqttClient::Transport transport{};
            transport.connect = [](const yaha::YahaMqttClient::Config&) { return true; };
            transport.disconnect = []() {
                throw std::runtime_error{"disconnect boom"};
            };
            transport.publish = nullptr;
            transport.subscribe = [](const std::string&, const yaha::Qos) -> bool {
                throw std::runtime_error{"subscribe boom"};
            };
            transport.unsubscribe = [](const std::string&) -> bool {
                throw k_unknown_unsubscribe_error_marker;
            };
            transport.pollIncoming = nullptr;
            transport.ping = []() {
                throw k_unknown_ping_error_marker;
            };
            transport.isConnected = []() {
                return false;
            };
            return transport;
        }};

    yaha::HttpMqttSessionConnectRequest request{};
    request.clientId = "client-a";

    yaha::HttpMqttSessionConnectTokens tokens{};
    std::string errorText{};
    REQUIRE(manager.connect(request, tokens, errorText));

    std::vector<std::uint8_t> codes{};
    std::map<std::string, yaha::Qos> topics{{"demo/topic", yaha::Qos::AtLeastOnce}};
    std::optional<yaha::Message> message{};

    REQUIRE_FALSE(manager.subscribe(tokens.sendToken, topics, codes, errorText));
    REQUIRE(errorText == "subscribe boom");

    REQUIRE_FALSE(manager.unsubscribe(tokens.sendToken, topics, codes, errorText));
    REQUIRE(errorText == "unknown broker unsubscribe failure");

    REQUIRE_FALSE(manager.publish(tokens.sendToken, yaha::Message{"demo/topic", std::string{"x"}}, errorText));
    REQUIRE(errorText == "publish transport not configured");

    REQUIRE_FALSE(manager.receive(tokens.sendToken, message, errorText));
    REQUIRE(errorText == "receive transport not configured");

    REQUIRE_FALSE(manager.ping(tokens.sendToken, errorText));
    REQUIRE(errorText == "unknown broker ping failure");

    REQUIRE_FALSE(manager.disconnect(tokens.sendToken, errorText));
    REQUIRE(errorText == "disconnect boom");

    REQUIRE_FALSE(manager.disconnect("unknown-token", errorText));
    REQUIRE(errorText == "unknown token");
    REQUIRE_FALSE(manager.subscribe("unknown-token", topics, codes, errorText));
    REQUIRE(errorText == "unknown token");
    REQUIRE_FALSE(manager.unsubscribe("unknown-token", topics, codes, errorText));
    REQUIRE(errorText == "unknown token");
    REQUIRE_FALSE(manager.publish("unknown-token", yaha::Message{"demo/topic", std::string{"x"}}, errorText));
    REQUIRE(errorText == "unknown token");
    REQUIRE_FALSE(manager.receive("unknown-token", message, errorText));
    REQUIRE(errorText == "unknown token");
    REQUIRE_FALSE(manager.ping("unknown-token", errorText));
    REQUIRE(errorText == "unknown token");
}
