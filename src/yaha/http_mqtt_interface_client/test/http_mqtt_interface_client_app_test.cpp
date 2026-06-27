#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "json/json_value.h"

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_http_mqtt_interface_client_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

constexpr int k_wait_attempts{20};
constexpr int k_wait_sleep_ms{5};
constexpr int k_http_timeout_microseconds{50000};
constexpr int k_status_ok{200};
constexpr int k_status_no_content{204};
constexpr int k_status_internal_server_error{500};
constexpr std::uint16_t k_test_broker_port{1883U};
constexpr unsigned int k_test_port_base{30000U};
constexpr unsigned int k_test_port_range{20000U};

std::atomic<unsigned int> g_next_test_port{k_test_port_base};

void configureHttpClientTimeouts(httplib::Client& client) {
    client.set_keep_alive(false);
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
}

[[nodiscard]] std::uint16_t reserveFreeLocalPort() {
    const unsigned int rawPort = g_next_test_port.fetch_add(1U);
    const unsigned int normalizedPort = k_test_port_base + (rawPort % k_test_port_range);
    return static_cast<std::uint16_t>(normalizedPort);
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

class RuntimeHarness {
public:
    RuntimeHarness(
        yaha::HttpMqttInterfaceClientConfig configInput,
        yaha::YahaMqttClient::Transport brokerTransport)
        : config(std::move(configInput))
        , component(config)
        , mqttClient(config.mqttConfig, component, std::move(brokerTransport))
        , runtime(mqttClient, component) {}

    void start() {
        runtimeThread = std::thread([this]() {
            try {
                runtime.runUntilSignal();
                exitCode.store(0);
            } catch (const std::exception& exceptionValue) {
                exceptionText = exceptionValue.what();
                exitCode.store(1);
            } catch (...) {
                exceptionText = "unknown";
                exitCode.store(1);
            }
        });
    }

    void stop() {
        yaha::YahaMqttClientRuntime::requestShutdown();
        if (runtimeThread.joinable()) {
            runtimeThread.join();
        }
    }

    ~RuntimeHarness() {
        if (runtimeThread.joinable()) {
            stop();
        }
    }

    [[nodiscard]] int resultCode() const {
        return exitCode.load();
    }

    [[nodiscard]] const std::string& errorText() const {
        return exceptionText;
    }

private:
public:
    yaha::HttpMqttInterfaceClientConfig config;
    yaha::HttpMqttInterfaceClientComponent component;
    yaha::YahaMqttClient mqttClient;
    yaha::YahaMqttClientRuntime runtime;
    std::thread runtimeThread{};
    std::atomic<int> exitCode{-1};
    std::string exceptionText{};
};

yaha::YahaMqttClient::Transport makeMockTransport(
    const std::function<void(const yaha::Message&)>& publishFn,
    const std::function<bool()>& connectFn = []() { return true; }) {
    yaha::YahaMqttClient::Transport mockTransport{};
    mockTransport.connect = [connectFn](const yaha::YahaMqttClient::Config&) -> bool {
        return connectFn();
    };
    mockTransport.disconnect = []() {};
    mockTransport.publish = publishFn;
    mockTransport.subscribe = [](const std::string&, yaha::Qos) -> bool { return true; };
    mockTransport.unsubscribe = [](const std::string&) -> bool { return true; };
    mockTransport.pollIncoming = []() -> std::optional<yaha::Message> { return std::nullopt; };
    mockTransport.ping = []() {};
    mockTransport.isConnected = []() -> bool { return true; };
    return mockTransport;
}

struct SessionMockState {
    bool connected{false};
    std::string clientId{};
    std::map<std::string, yaha::Qos> subscriptions{};
    std::deque<yaha::Message> inbox{};
    int publishCalls{0};
    int subscribeCalls{0};
    int unsubscribeCalls{0};
    int pingCalls{0};
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
                ++state->publishCalls;
                state->inbox.push_back(message);
            };
            transport.subscribe = [state](const std::string& topicFilter, const yaha::Qos qos) {
                ++state->subscribeCalls;
                state->subscriptions[topicFilter] = qos;
                return true;
            };
            transport.unsubscribe = [state](const std::string& topicFilter) {
                ++state->unsubscribeCalls;
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
            transport.ping = [state]() {
                ++state->pingCalls;
            };
            transport.isConnected = [state]() {
                return state->connected;
            };

            return transport;
        };
    }
};

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

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_http_mqtt_interface_client_config_defaults", "[http_mqtt_interface_client]") {
    const auto iniPath = writeTempIni("");
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.listenerHost == "127.0.0.1");
    REQUIRE(config.listenerPort == 8092U);
    REQUIRE(config.enablePublishPhpAlias);
    REQUIRE_FALSE(config.useLegacyPhpResponse);
    REQUIRE(config.logIncomingRequests);
    REQUIRE(config.logEvents);
    REQUIRE(config.logErrors);
    REQUIRE(config.logBrokerMessages);
    REQUIRE(config.connectedClientsReportIntervalSeconds == 60U);

    std::filesystem::remove(iniPath);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_http_mqtt_interface_client_config_from_ini", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "listenerHost=0.0.0.0\n"
        "listenerPort=8123\n"
        "enablePublishPhpAlias=false\n"
        "useLegacyPhpResponse=true\n"
        "logIncomingRequests=false\n"
        "logEvents=false\n"
        "logErrors=false\n"
        "logBrokerMessages=false\n"
        "connectedClientsReportIntervalSeconds=120\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.listenerHost == "0.0.0.0");
    REQUIRE(config.listenerPort == 8123U);
    REQUIRE_FALSE(config.enablePublishPhpAlias);
    REQUIRE(config.useLegacyPhpResponse);
    REQUIRE_FALSE(config.logIncomingRequests);
    REQUIRE_FALSE(config.logEvents);
    REQUIRE_FALSE(config.logErrors);
    REQUIRE_FALSE(config.logBrokerMessages);
    REQUIRE(config.connectedClientsReportIntervalSeconds == 120U);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_falls_back_on_invalid_port", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "listenerPort=70000\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.listenerPort == 8092U);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_falls_back_on_invalid_alias_flag", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "enablePublishPhpAlias=maybe\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.enablePublishPhpAlias);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_falls_back_on_invalid_legacy_flag", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "useLegacyPhpResponse=invalid\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(config.useLegacyPhpResponse);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_falls_back_on_invalid_log_events_flag", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "logEvents=invalid\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.logEvents);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_falls_back_on_invalid_mqtt_value", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "loopSleepMs=0\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.mqttConfig.loopSleep == std::chrono::milliseconds{20});

    std::filesystem::remove(iniPath);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_logs_broker_publish_error_when_ack_missing", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    std::ostringstream capturedOutput{};
    std::streambuf* previousOutputBuffer = std::cout.rdbuf(capturedOutput.rdbuf());
    std::ostringstream capturedErrorOutput{};
    std::streambuf* previousErrorBuffer = std::cerr.rdbuf(capturedErrorOutput.rdbuf());

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.enablePublishPhpAlias = true;
    config.useLegacyPhpResponse = false;

    yaha::HttpMqttInterfaceClientComponent component{config};
    yaha::YahaMqttClient mqttClient{
        config.mqttConfig,
        component,
        makeMockTransport([](const yaha::Message&) {
            throw std::runtime_error{"timed out waiting for PUBACK from broker"};
        })};
    mqttClient.run();
    component.run();

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    const httplib::Params formParams{{"topic", "sensor%2Ftemp"}, {"value", "42"}, {"token", "tok-form"}};
    const auto postResponse = client.Post("/publish", formParams);
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == k_status_internal_server_error);

    component.close();
    mqttClient.close();
    std::cout.rdbuf(previousOutputBuffer);
    std::cerr.rdbuf(previousErrorBuffer);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("component=\"http_mqtt_interface_client\" direction=\"outgoing\"") != std::string::npos);
    REQUIRE(outputText.find("event=broker_publish_failed") != std::string::npos);
    REQUIRE(outputText.find("detail=message_was_sent_but_broker_reported_no_ack") != std::string::npos);

    const std::string errorOutputText = capturedErrorOutput.str();
    REQUIRE(errorOutputText.find("http_mqtt_interface_client[error] publish_request_failed endpoint=/publish") !=
        std::string::npos);
}

TEST_CASE("http_mqtt_interface_component_returns_error_on_listen_failure", "[http_mqtt_interface_client]") {
    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "invalid.invalid.invalid";
    config.listenerPort = reserveFreeLocalPort();

    yaha::HttpMqttInterfaceClientComponent component{config};
    yaha::YahaMqttClient mqttClient{
        config.mqttConfig,
        component,
        makeMockTransport([](const yaha::Message&) {})};
    yaha::YahaMqttClientRuntime runtime{mqttClient, component};

    REQUIRE_THROWS_AS(runtime.runUntilSignal(), yaha::YahaError);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_recovers_across_repeated_broker_publish_failures", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    std::atomic<int> publishCallCount{0};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.enablePublishPhpAlias = false;
    config.useLegacyPhpResponse = false;

    RuntimeHarness harness{config, makeMockTransport([&publishCallCount](const yaha::Message&) {
                             const int currentCall = ++publishCallCount;
                             if ((currentCall % 2) == 1) {
                                 throw std::runtime_error{"timed out waiting for PUBACK from broker"};
                             }
                         })};
    harness.start();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    const httplib::Params formParams{{"topic", "home%2Fstate"}, {"value", "1"}};

    const auto firstResponse = client.Post("/publish", formParams);
    REQUIRE(firstResponse != nullptr);
    REQUIRE(firstResponse->status == k_status_internal_server_error);

    const auto secondResponse = client.Post("/publish", formParams);
    REQUIRE(secondResponse != nullptr);
    REQUIRE(secondResponse->status == k_status_no_content);

    const auto thirdResponse = client.Post("/publish", formParams);
    REQUIRE(thirdResponse != nullptr);
    REQUIRE(thirdResponse->status == k_status_internal_server_error);

    const auto fourthResponse = client.Post("/publish", formParams);
    REQUIRE(fourthResponse != nullptr);
    REQUIRE(fourthResponse->status == k_status_no_content);

    REQUIRE(publishCallCount.load() == 4);

    harness.stop();
    REQUIRE(harness.resultCode() == 0);
}

TEST_CASE("http_mqtt_interface_component_put_publish_missing_topic_returns_400", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    RuntimeHarness harness{config, makeMockTransport([](const yaha::Message&) {})};
    harness.start();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto putResponse = client.Put("/publish", "{}", "application/json");
    REQUIRE(putResponse != nullptr);
    REQUIRE(putResponse->status == 400);
    REQUIRE(putResponse->body.find("missing_topic") != std::string::npos);

    harness.stop();
    REQUIRE(harness.resultCode() == 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_put_publish_forwards_to_managed_session", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};
    std::atomic<int> legacyPublishCalls{0};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.setPublishCallback([&legacyPublishCalls](const yaha::Message&) {
        ++legacyPublishCalls;
        return yaha::PublishResult::ok();
    });

    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"http-put-publisher"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());
    REQUIRE_FALSE(maybeSendToken->empty());

    const std::string publishBody =
        std::string{R"({"token":")"} + *maybeSendToken +
        R"(","topic":"put/demo/topic","value":"42"})";
    const auto publishResponse = client.Put(
        "/publish",
        httplib::Headers{{"version", "1.0"}},
        publishBody,
        "application/json");
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_status_no_content);

    const std::string receiveBody = std::string{R"({"token":")"} + *maybeSendToken + R"("})";
    const auto receiveResponse = client.Put(
        "/receive",
        httplib::Headers{{"version", "1.0"}},
        receiveBody,
        "application/json");
    REQUIRE(receiveResponse != nullptr);
    REQUIRE(receiveResponse->status == k_status_ok);
    REQUIRE(receiveResponse->body.find("put/demo/topic") != std::string::npos);

    component.close();

    REQUIRE(legacyPublishCalls.load() == 0);
    REQUIRE(sessionFactory.states.size() == 1);
    REQUIRE(sessionFactory.states.front()->publishCalls == 1);
}

TEST_CASE("http_mqtt_interface_component_put_pubrel_failure_returns_500_and_logs", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    std::ostringstream capturedErrorOutput{};
    std::streambuf* previousErrorBuffer = std::cerr.rdbuf(capturedErrorOutput.rdbuf());

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    RuntimeHarness harness{config, makeMockTransport([](const yaha::Message&) {})};
    harness.start();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto putResponse = client.Put("/pubrel", "{}", "application/json");
    REQUIRE(putResponse != nullptr);
    REQUIRE(putResponse->status == k_status_internal_server_error);

    harness.stop();
    std::cerr.rdbuf(previousErrorBuffer);

    const std::string errorOutputText = capturedErrorOutput.str();
    REQUIRE(errorOutputText.find("publish_request_failed endpoint=/pubrel") != std::string::npos);
    REQUIRE(harness.resultCode() == 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_session_commands_flow", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};
    std::atomic<int> legacyPublishCalls{0};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.setPublishCallback([&legacyPublishCalls](const yaha::Message&) {
        ++legacyPublishCalls;
        return yaha::PublishResult::ok();
    });

    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"http-client-1"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());
    REQUIRE_FALSE(maybeSendToken->empty());

    const std::string subscribeBody =
        std::string{R"({"token":")"} + *maybeSendToken +
        R"(","packetid":42,"topics":{"demo/topic":1}})";
    const auto subscribeResponse = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "1.0"}},
        subscribeBody,
        "application/json");
    REQUIRE(subscribeResponse != nullptr);
    REQUIRE(subscribeResponse->status == k_status_ok);

    const httplib::Params publishParams{
        {"token", *maybeSendToken},
        {"topic", "demo%2Ftopic"},
        {"value", "42"},
    };
    const auto publishResponse = client.Post("/publish", publishParams);
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_status_no_content);

    const std::string receiveBody = std::string{R"({"token":")"} + *maybeSendToken + R"("})";
    const auto receiveResponse = client.Put(
        "/receive",
        httplib::Headers{{"version", "1.0"}},
        receiveBody,
        "application/json");
    REQUIRE(receiveResponse != nullptr);
    REQUIRE(receiveResponse->status == k_status_ok);
    REQUIRE(receiveResponse->body.find("demo/topic") != std::string::npos);

    const auto pingResponse = client.Put(
        "/pingreq",
        httplib::Headers{{"version", "1.0"}},
        receiveBody,
        "application/json");
    REQUIRE(pingResponse != nullptr);
    REQUIRE(pingResponse->status == k_status_no_content);

    const std::string unsubscribeBody =
        std::string{R"({"token":")"} + *maybeSendToken +
        R"(","packetid":43,"topics":{"demo/topic":1}})";
    const auto unsubscribeResponse = client.Put(
        "/unsubscribe",
        httplib::Headers{{"version", "1.0"}},
        unsubscribeBody,
        "application/json");
    REQUIRE(unsubscribeResponse != nullptr);
    REQUIRE(unsubscribeResponse->status == k_status_ok);

    const auto disconnectResponse = client.Put(
        "/disconnect",
        httplib::Headers{{"version", "1.0"}},
        receiveBody,
        "application/json");
    REQUIRE(disconnectResponse != nullptr);
    REQUIRE(disconnectResponse->status == k_status_no_content);

    component.close();

    REQUIRE(legacyPublishCalls.load() == 0);
    REQUIRE(sessionFactory.states.size() == 1);
    REQUIRE(sessionFactory.states.front()->publishCalls == 1);
    REQUIRE(sessionFactory.states.front()->subscribeCalls == 1);
    REQUIRE(sessionFactory.states.front()->unsubscribeCalls == 1);
    REQUIRE(sessionFactory.states.front()->pingCalls == 1);
}

TEST_CASE("http_mqtt_interface_component_compat_publish_without_managed_token_uses_legacy_callback", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};
    std::atomic<int> legacyPublishCalls{0};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.setPublishCallback([&legacyPublishCalls](const yaha::Message&) {
        ++legacyPublishCalls;
        return yaha::PublishResult::ok();
    });

    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const httplib::Params publishParams{
        {"topic", "fallback%2Ftopic"},
        {"value", "1"},
    };
    const auto publishResponse = client.Post("/publish", publishParams);
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_status_no_content);

    component.close();

    REQUIRE(legacyPublishCalls.load() == 1);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_command_endpoints_validate_bad_requests", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectInvalidJson = client.Put("/connect", "{", "application/json");
    REQUIRE(connectInvalidJson != nullptr);
    REQUIRE(connectInvalidJson->status == 400);
    REQUIRE(connectInvalidJson->body.find("invalid_json") != std::string::npos);

    const auto connectMissingClientId = client.Put("/connect", "{}", "application/json");
    REQUIRE(connectMissingClientId != nullptr);
    REQUIRE(connectMissingClientId->status == 400);
    REQUIRE(connectMissingClientId->body.find("missing_client_id") != std::string::npos);

    const auto subscribeMissingTopics = client.Put("/subscribe", R"({"token":"abc"})", "application/json");
    REQUIRE(subscribeMissingTopics != nullptr);
    REQUIRE(subscribeMissingTopics->status == 400);
    REQUIRE(subscribeMissingTopics->body.find("invalid_topics") != std::string::npos);

    const auto unsubscribeMissingToken = client.Put(
        "/unsubscribe",
        R"({"topics":{"demo/topic":1}})",
        "application/json");
    REQUIRE(unsubscribeMissingToken != nullptr);
    REQUIRE(unsubscribeMissingToken->status == 400);
    REQUIRE(unsubscribeMissingToken->body.find("missing_token") != std::string::npos);

    const auto disconnectMissingToken = client.Put("/disconnect", "{}", "application/json");
    REQUIRE(disconnectMissingToken != nullptr);
    REQUIRE(disconnectMissingToken->status == 400);
    REQUIRE(disconnectMissingToken->body.find("missing_token") != std::string::npos);

    const auto pingMissingToken = client.Put("/pingreq", "{}", "application/json");
    REQUIRE(pingMissingToken != nullptr);
    REQUIRE(pingMissingToken->status == 400);
    REQUIRE(pingMissingToken->body.find("missing_token") != std::string::npos);

    const auto receiveMissingToken = client.Put("/receive", "{}", "application/json");
    REQUIRE(receiveMissingToken != nullptr);
    REQUIRE(receiveMissingToken->status == 400);
    REQUIRE(receiveMissingToken->body.find("missing_token") != std::string::npos);

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_command_response_mapping_handles_unsupported_version", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        httplib::Headers{{"version", "9.9"}},
        R"({"clientId":"client-unsupported"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == 400);
    REQUIRE(connectResponse->body.find("unsupported_version") != std::string::npos);

    const auto subscribeResponse = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "9.9"}},
        R"({"token":"unknown","topics":{"demo/topic":1},"packetid":1})",
        "application/json");
    REQUIRE(subscribeResponse != nullptr);
    REQUIRE(subscribeResponse->status == 400);
    REQUIRE(subscribeResponse->body.find("unsupported_version") != std::string::npos);

    const auto unsubscribeResponse = client.Put(
        "/unsubscribe",
        httplib::Headers{{"version", "9.9"}},
        R"({"token":"unknown","topics":{"demo/topic":1},"packetid":1})",
        "application/json");
    REQUIRE(unsubscribeResponse != nullptr);
    REQUIRE(unsubscribeResponse->status == 400);
    REQUIRE(unsubscribeResponse->body.find("unsupported_version") != std::string::npos);

    const auto disconnectResponse = client.Put(
        "/disconnect",
        httplib::Headers{{"version", "9.9"}},
        R"({"token":"unknown"})",
        "application/json");
    REQUIRE(disconnectResponse != nullptr);
    REQUIRE(disconnectResponse->status == 400);
    REQUIRE(disconnectResponse->body.find("unsupported_version") != std::string::npos);

    component.close();
}

TEST_CASE("http_mqtt_interface_component_compat_publish_managed_session_failure_returns_500", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();

    auto sessionFactory = []() {
        yaha::YahaMqttClient::Transport transport{};
        transport.connect = [](const yaha::YahaMqttClient::Config&) { return true; };
        transport.disconnect = []() {};
        transport.publish = [](const yaha::Message&) {
            throw std::runtime_error{"timed out waiting for PUBACK from broker"};
        };
        transport.subscribe = [](const std::string&, const yaha::Qos) { return true; };
        transport.unsubscribe = [](const std::string&) { return true; };
        transport.pollIncoming = []() -> std::optional<yaha::Message> { return std::nullopt; };
        transport.ping = []() {};
        transport.isConnected = []() { return true; };
        return transport;
    };

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory};
    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"managed-failing"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == 200);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());

    const httplib::Params publishParams{
        {"token", *maybeSendToken},
        {"topic", "demo%2Ftopic"},
        {"value", "1"},
    };
    const auto publishResponse = client.Post("/publish", publishParams);
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == 500);

    component.close();
}

TEST_CASE("http_mqtt_interface_component_connect_legacy_host_port_do_not_override_broker_target", "[http_mqtt_interface_client]") {
    struct CapturedConnectConfig {
        std::string brokerHost{};
        std::uint16_t brokerPort{0U};
    };

    const std::uint16_t port = reserveFreeLocalPort();
    auto captured = std::make_shared<CapturedConnectConfig>();

    auto sessionFactory = [captured]() {
        yaha::YahaMqttClient::Transport transport{};
        transport.connect = [captured](const yaha::YahaMqttClient::Config& config) {
            captured->brokerHost = config.brokerHost;
            captured->brokerPort = config.brokerPort;
            return true;
        };
        transport.disconnect = []() {};
        transport.publish = [](const yaha::Message&) {};
        transport.subscribe = [](const std::string&, const yaha::Qos) { return true; };
        transport.unsubscribe = [](const std::string&) { return true; };
        transport.pollIncoming = []() -> std::optional<yaha::Message> { return std::nullopt; };
        transport.ping = []() {};
        transport.isConnected = []() { return true; };
        return transport;
    };

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.mqttConfig.brokerHost = "configured-broker";
    config.mqttConfig.brokerPort = k_test_broker_port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        R"({"clientId":"ts-compat-connect","host":"10.0.0.99","port":49999,"brokerHost":"127.0.0.1","brokerPort":1883})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    REQUIRE(captured->brokerHost == "127.0.0.1");
    REQUIRE(captured->brokerPort == k_test_broker_port);

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_legacy_clientid_commands_without_token", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        R"({"clientId":"legacy-clientid-flow"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    const auto subscribeResponse = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"legacy-clientid-flow","packetid":11,"topics":{"demo/topic":1}})",
        "application/json");
    REQUIRE(subscribeResponse != nullptr);
    REQUIRE(subscribeResponse->status == k_status_ok);

    const auto unsubscribeResponse = client.Put(
        "/unsubscribe",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"legacy-clientid-flow","packetid":12,"topics":{"demo/topic":1}})",
        "application/json");
    REQUIRE(unsubscribeResponse != nullptr);
    REQUIRE(unsubscribeResponse->status == k_status_ok);

    const auto disconnectResponse = client.Put(
        "/disconnect",
        httplib::Headers{{"version", "1.0"}},
        R"({"clientId":"legacy-clientid-flow"})",
        "application/json");
    REQUIRE(disconnectResponse != nullptr);
    REQUIRE(disconnectResponse->status == k_status_no_content);

    component.close();

    REQUIRE(sessionFactory.states.size() == 1);
    REQUIRE(sessionFactory.states.front()->subscribeCalls == 1);
    REQUIRE(sessionFactory.states.front()->unsubscribeCalls == 1);
}

TEST_CASE("http_mqtt_interface_component_command_options_preflight_endpoints_return_204", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const std::array<std::string, 6> endpoints{
        "/connect",
        "/disconnect",
        "/subscribe",
        "/unsubscribe",
        "/pingreq",
        "/receive",
    };

    for (const auto& endpoint : endpoints) {
        const auto response = client.Options(endpoint);
        REQUIRE(response != nullptr);
        REQUIRE(response->status == k_status_no_content);
        REQUIRE(response->get_header_value("Access-Control-Allow-Origin") == "*");
    }

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_receive_without_message_returns_204", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        R"({"clientId":"receive-empty"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());

    const std::string receiveBody = std::string{R"({"token":")"} + *maybeSendToken + R"("})";
    const auto receiveResponse = client.Put("/receive", receiveBody, "application/json");
    REQUIRE(receiveResponse != nullptr);
    REQUIRE(receiveResponse->status == k_status_no_content);
    REQUIRE(receiveResponse->get_header_value("packet").empty());

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_command_response_failed_paths_with_valid_session", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto connectResponse = client.Put(
        "/connect",
        R"({"clientId":"response-failed"})",
        "application/json");
    REQUIRE(connectResponse != nullptr);
    REQUIRE(connectResponse->status == k_status_ok);

    const auto maybeSendToken = tryReadConnectSendToken(connectResponse->body);
    REQUIRE(maybeSendToken.has_value());

    const std::string subscribeBody =
        std::string{R"({"token":")"} + *maybeSendToken +
        R"(","packetid":1,"topics":{"demo/topic":1}})";
    const auto subscribeResponse = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "9.9"}},
        subscribeBody,
        "application/json");
    REQUIRE(subscribeResponse != nullptr);
    REQUIRE(subscribeResponse->status == 400);
    REQUIRE(subscribeResponse->body.find("unsupported_version") != std::string::npos);

    const std::string unsubscribeBody =
        std::string{R"({"token":")"} + *maybeSendToken +
        R"(","packetid":2,"topics":{"demo/topic":1}})";
    const auto unsubscribeResponse = client.Put(
        "/unsubscribe",
        httplib::Headers{{"version", "9.9"}},
        unsubscribeBody,
        "application/json");
    REQUIRE(unsubscribeResponse != nullptr);
    REQUIRE(unsubscribeResponse->status == 400);
    REQUIRE(unsubscribeResponse->body.find("unsupported_version") != std::string::npos);

    const std::string disconnectBody = std::string{R"({"token":")"} + *maybeSendToken + R"("})";
    const auto disconnectResponse = client.Put(
        "/disconnect",
        httplib::Headers{{"version", "9.9"}},
        disconnectBody,
        "application/json");
    REQUIRE(disconnectResponse != nullptr);
    REQUIRE(disconnectResponse->status == 400);
    REQUIRE(disconnectResponse->body.find("unsupported_version") != std::string::npos);

    component.close();
}

TEST_CASE("http_mqtt_interface_component_subscribe_version_zero_returns_unsupported_version", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    SessionMockFactory sessionFactory{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;

    yaha::HttpMqttInterfaceClientComponent component{config, sessionFactory.makeFactory()};
    component.run();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto response = client.Put(
        "/subscribe",
        httplib::Headers{{"version", "0"}},
        R"({"token":"abc"})",
        "application/json");
    REQUIRE(response != nullptr);
    REQUIRE(response->status == 400);
    REQUIRE(response->body.find("unsupported_version") != std::string::npos);

    component.close();
}
