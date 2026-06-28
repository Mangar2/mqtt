#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"

#include "json/json_value.h"
#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/mqtt_client/mqtt_client.h"

namespace {

constexpr int k_wait_attempts{40};
constexpr int k_wait_sleep_ms{25};
constexpr int k_http_timeout_microseconds{50000};
constexpr int k_status_ok{200};
constexpr int k_status_no_content{204};
constexpr unsigned int k_test_port_seed{33000U};
constexpr unsigned int k_test_port_base{30000U};
constexpr unsigned int k_test_port_range{20000U};
constexpr int k_callback_startup_wait_ms{50};
constexpr int k_report_interval_wait_ms{1100};

std::atomic<unsigned int> g_next_test_port_coverage{k_test_port_seed};

void configureHttpClientTimeouts(httplib::Client& client) {
    client.set_keep_alive(false);
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
}

[[nodiscard]] std::uint16_t reserveFreeLocalPort() {
    const unsigned int rawPort = g_next_test_port_coverage.fetch_add(1U);
    return static_cast<std::uint16_t>(k_test_port_base + (rawPort % k_test_port_range));
}

bool waitForHttpServer(const std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    for (int attempt = 0; attempt < k_wait_attempts; ++attempt) {
        if (const auto response = client.Get("/health")) {
            return response->status == k_status_ok;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_wait_sleep_ms});
    }
    return false;
}

[[nodiscard]] std::optional<std::string> tryReadConnectSendToken(const std::string& payload) {
    const auto parsed = mqtt::json::JsonValue::try_parse(payload);
    if (!parsed.has_value() || !parsed->is_object() || !parsed->contains("token")) {
        return std::nullopt;
    }

    const auto& tokenObject = parsed->at("token");
    if (!tokenObject.is_object() || !tokenObject.contains("send")) {
        return std::nullopt;
    }

    const auto& sendToken = tokenObject.at("send");
    if (!sendToken.is_string()) {
        return std::nullopt;
    }

    return sendToken.as_string();
}

struct SessionMockState {
    bool connected{false};
    std::string clientId{};
    std::map<std::string, yaha::Qos> subscriptions{};
    std::deque<yaha::Message> inbox{};
};

struct SessionMockFactory {
    std::mutex mutex{};
    std::vector<std::shared_ptr<SessionMockState>> states{};

    [[nodiscard]] yaha::HttpMqttSessionTransportFactory makeFactory() {
        return [this]() {
            auto state = std::make_shared<SessionMockState>();
            {
                std::lock_guard<std::mutex> lock{mutex};
                states.push_back(state);
            }

            yaha::YahaMqttClient::Transport transport{};
            transport.connect = [state](const yaha::YahaMqttClient::Config& config) {
                state->connected = true;
                state->clientId = config.clientId;
                return true;
            };
            transport.disconnect = [state]() {
                state->connected = false;
            };
            transport.publish = [state](const yaha::Message& message) {
                state->inbox.push_back(message);
            };
            transport.subscribe = [state](const std::string& topicFilter, const yaha::Qos qos) {
                state->subscriptions[topicFilter] = qos;
                return true;
            };
            transport.unsubscribe = [state](const std::string& topicFilter) {
                state->subscriptions.erase(topicFilter);
                return true;
            };
            transport.pollIncoming = [state]() -> std::optional<yaha::Message> {
                if (state->inbox.empty()) {
                    return std::nullopt;
                }
                yaha::Message message = state->inbox.front();
                state->inbox.pop_front();
                return message;
            };
            transport.ping = []() {};
            transport.isConnected = [state]() {
                return state->connected;
            };
            return transport;
        };
    }
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_forwards_listener_and_reports_connected_clients", "[http_mqtt_interface_client]") {
    const std::uint16_t httpPort = reserveFreeLocalPort();
    const std::uint16_t callbackPort = reserveFreeLocalPort();

    SessionMockFactory sessionFactory{};

    std::atomic<int> callbackPublishCount{0};
    std::mutex callbackBodyMutex{};
    std::string callbackBody{};

    httplib::Server callbackServer{};
    callbackServer.Put("/publish", [&](const httplib::Request& request, httplib::Response& response) {
        {
            std::lock_guard<std::mutex> lock{callbackBodyMutex};
            callbackBody = request.body;
        }
        ++callbackPublishCount;
        response.status = k_status_no_content;
    });

    std::thread callbackThread([&]() {
        callbackServer.listen("127.0.0.1", callbackPort);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{k_callback_startup_wait_ms});

    std::ostringstream capturedOutput{};
    std::streambuf* previousOutputBuffer = std::cout.rdbuf(capturedOutput.rdbuf());

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = httpPort;
    config.connectedClientsReportIntervalSeconds = 1U;
    config.logEvents = true;
    config.logErrors = true;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(httpPort));

    httplib::Client client{"127.0.0.1", static_cast<int>(httpPort)};
    configureHttpClientTimeouts(client);

    const std::string connectBody =
        std::string{R"({"clientId":"listener-forward-client","host":"127.0.0.1","port":)"} +
        std::to_string(callbackPort) +
        R"(})";
    const auto connectResponse = client.Put("/connect", connectBody, "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == 200);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());

    const httplib::Params publishParams{{"token", *maybeSendToken}, {"topic", "forward%2Ftopic"}, {"value", "42"}};
    const auto publishResponse = client.Post("/publish", publishParams);
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == 204);

    bool gotForward = false;
    for (int attempt = 0; attempt < k_wait_attempts; ++attempt) {
        if (callbackPublishCount.load() > 0) {
            gotForward = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_wait_sleep_ms});
    }
    REQUIRE(gotForward);

    // Allow one report interval so connected-clients reporting branch executes.
    std::this_thread::sleep_for(std::chrono::milliseconds{k_report_interval_wait_ms});

    component.close();

    callbackServer.stop();
    if (callbackThread.joinable()) {
        callbackThread.join();
    }

    std::cout.rdbuf(previousOutputBuffer);

    std::string forwardedBody{};
    {
        std::lock_guard<std::mutex> lock{callbackBodyMutex};
        forwardedBody = callbackBody;
    }

    REQUIRE(forwardedBody.find("forward/topic") != std::string::npos);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("listener_forward") != std::string::npos);
    REQUIRE(outputText.find("connected_clients_report") != std::string::npos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_subscribe_shapes_handle_qos_edge_values", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.logEvents = false;
    config.logErrors = false;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        R"({"clientId":"shape-qos-client"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == 200);

    const auto subscribeQos0Response = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"shape-qos-client","subscribe":{"QoS":"0","topics":["shape/topic/0"]},"packetid":1})",
        "application/json");
    REQUIRE(subscribeQos0Response != nullptr);
    REQUIRE(subscribeQos0Response->status == 200);

    const auto unsubscribeQos2Response = client.Put(
        "/unsubscribe",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"shape-qos-client","unsubscribe":{"QoS":"2","topics":"shape/topic/0"},"packetid":2})",
        "application/json");
    REQUIRE(unsubscribeQos2Response != nullptr);
    REQUIRE(unsubscribeQos2Response->status == 200);

    const auto subscribeInvalidQosResponse = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"shape-qos-client","subscribe":{"QoS":"9","topics":"shape/topic/1"},"packetid":3})",
        "application/json");
    REQUIRE(subscribeInvalidQosResponse != nullptr);
    REQUIRE(subscribeInvalidQosResponse->status == 400);
    REQUIRE(subscribeInvalidQosResponse->body.find("invalid_topics") != std::string::npos);

    component.close();
}
