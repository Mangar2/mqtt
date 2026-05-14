#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "httplib.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_http_mqtt_interface_client_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

constexpr std::uint16_t k_fallback_test_port{28130U};
constexpr int k_wait_attempts{50};
constexpr int k_wait_sleep_ms{10};
constexpr int k_http_timeout_microseconds{500000};
constexpr int k_status_ok{200};
constexpr int k_status_no_content{204};
constexpr int k_status_internal_server_error{500};
constexpr const char* k_expected_cors_methods{"POST, PUT, OPTIONS"};
constexpr const char* k_expected_cors_headers{"Content-Type, Authorization, X-Requested-With"};

void configureHttpClientTimeouts(httplib::Client& client) {
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
}

[[nodiscard]] std::uint16_t reserveFreeLocalPort() {
    httplib::Server probeServer;
    const int boundPort = probeServer.bind_to_any_port("127.0.0.1");
    if (boundPort <= 0) {
        return k_fallback_test_port;
    }
    const auto port = static_cast<std::uint16_t>(boundPort);
    probeServer.stop();
    return port;
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

void verifyCorsHeaders(const httplib::Result& response) {
    REQUIRE(response != nullptr);
    REQUIRE(response->get_header_value("Access-Control-Allow-Origin") == "*");
    REQUIRE(response->get_header_value("Access-Control-Allow-Methods") == k_expected_cors_methods);
    REQUIRE(response->get_header_value("Access-Control-Allow-Headers") == k_expected_cors_headers);
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
        std::raise(SIGTERM);
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

} // namespace

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

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_from_ini", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "listenerHost=0.0.0.0\n"
        "listenerPort=8123\n"
        "enablePublishPhpAlias=false\n"
        "useLegacyPhpResponse=true\n";

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

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_reports_invalid_port", "[http_mqtt_interface_client]") {
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

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("httpMqttInterface.listenerPort") != std::string::npos);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_reports_invalid_alias_flag", "[http_mqtt_interface_client]") {
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

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("httpMqttInterface.enablePublishPhpAlias") != std::string::npos);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_reports_invalid_legacy_flag", "[http_mqtt_interface_client]") {
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

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("httpMqttInterface.useLegacyPhpResponse") != std::string::npos);

    std::filesystem::remove(iniPath);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("http_mqtt_interface_component_serves_endpoints_logs_publish_and_stops_on_signal", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    std::ostringstream capturedOutput{};
    std::streambuf* previousOutputBuffer = std::cout.rdbuf(capturedOutput.rdbuf());

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.enablePublishPhpAlias = true;
    config.useLegacyPhpResponse = false;

    RuntimeHarness harness{config, makeMockTransport([](const yaha::Message&) {})};
    harness.start();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);

    const auto healthResponse = client.Get("/health");
    REQUIRE(healthResponse != nullptr);
    REQUIRE(healthResponse->status == k_status_ok);

    const auto optionsPublishResponse = client.Options("/publish");
    REQUIRE(optionsPublishResponse != nullptr);
    REQUIRE(optionsPublishResponse->status == k_status_no_content);
    verifyCorsHeaders(optionsPublishResponse);
    REQUIRE(optionsPublishResponse->get_header_value("Access-Control-Max-Age") == "86400");

    const auto optionsPublishPhpResponse = client.Options("/publish.php");
    REQUIRE(optionsPublishPhpResponse != nullptr);
    REQUIRE(optionsPublishPhpResponse->status == k_status_no_content);
    verifyCorsHeaders(optionsPublishPhpResponse);

    const auto optionsPubrelResponse = client.Options("/pubrel");
    REQUIRE(optionsPubrelResponse != nullptr);
    REQUIRE(optionsPubrelResponse->status == k_status_no_content);
    verifyCorsHeaders(optionsPubrelResponse);

    const httplib::Headers putHeaders{
        {"version", "1.0"},
        {"qos", "1"},
        {"retain", "0"},
    };
    const auto putPublishResponse = client.Put("/publish", putHeaders, "{}", "application/json");
    REQUIRE(putPublishResponse != nullptr);
    REQUIRE(putPublishResponse->status == k_status_no_content);

    const auto putPubrelResponse = client.Put("/pubrel", httplib::Headers{{"version", "1.0"}}, "{}", "application/json");
    REQUIRE(putPubrelResponse != nullptr);
    REQUIRE(putPubrelResponse->status == k_status_no_content);

    const httplib::Params formParams{{"topic", "sensor%2Ftemp"}, {"value", "42"}, {"token", "tok-form"}};
    const auto postResponse = client.Post("/publish", formParams);
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == k_status_no_content);
    verifyCorsHeaders(postResponse);

    const httplib::Params formParamsNoToken{{"topic", "sensor%2Ffallback"}, {"value", "7"}};
    const auto postNoTokenResponse = client.Post("/publish", formParamsNoToken);
    REQUIRE(postNoTokenResponse != nullptr);
    REQUIRE(postNoTokenResponse->status == k_status_no_content);

    const std::string jsonBody =
        "{"
        "\"topic\":\"sensor%2Fjson\","
        "\"value\":2.5,"
        "\"qos\":2,"
        "\"retain\":false"
        "}";
    const auto postJsonResponse = client.Post("/publish", httplib::Headers{{"content-type", "application/json"}, {"token", "tok-json"}}, jsonBody, "application/json");
    REQUIRE(postJsonResponse != nullptr);
    REQUIRE(postJsonResponse->status == k_status_no_content);

    const auto postPhpResponse = client.Post("/publish.php", formParams);
    REQUIRE(postPhpResponse != nullptr);
    REQUIRE(postPhpResponse->status == k_status_no_content);

    harness.stop();
    std::cout.rdbuf(previousOutputBuffer);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("http_mqtt_interface_client[in] method=POST endpoint=/publish") != std::string::npos);
    REQUIRE(outputText.find("http_mqtt_interface_client[out] broker_publish_ack") != std::string::npos);
    REQUIRE(harness.resultCode() == 0);
}

TEST_CASE("http_mqtt_interface_component_run_twice_and_close_without_run_is_safe", "[http_mqtt_interface_client]") {
    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = reserveFreeLocalPort();

    yaha::HttpMqttInterfaceClientComponent component{config};
    component.close();

    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });
    component.run();
    component.run();

    component.handleMessage(yaha::Message{"ignore/topic", std::string{"value"}});
    component.close();
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

    RuntimeHarness harness{config, makeMockTransport([](const yaha::Message&) {
                             throw std::runtime_error{"timed out waiting for PUBACK from broker"};
                         })};
    harness.start();
    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    const httplib::Params formParams{{"topic", "sensor%2Ftemp"}, {"value", "42"}, {"token", "tok-form"}};
    const auto postResponse = client.Post("/publish", formParams);
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == k_status_internal_server_error);

    harness.stop();
    std::cout.rdbuf(previousOutputBuffer);
    std::cerr.rdbuf(previousErrorBuffer);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("http_mqtt_interface_client[error] broker_publish_failed") != std::string::npos);
    REQUIRE(outputText.find("detail=message_was_sent_but_broker_reported_no_ack") != std::string::npos);

    const std::string errorOutputText = capturedErrorOutput.str();
    REQUIRE(errorOutputText.find("http_mqtt_interface_client[error] publish_request_failed endpoint=/publish") !=
        std::string::npos);
    REQUIRE(harness.resultCode() == 0);
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
    const httplib::Params formParams{{"topic", "home%2Fstate"}, {"value", "1"}, {"token", "tok"}};

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

TEST_CASE("http_mqtt_interface_component_put_publish_failure_returns_500_and_logs", "[http_mqtt_interface_client]") {
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

    const auto putResponse = client.Put("/publish", "{}", "application/json");
    REQUIRE(putResponse != nullptr);
    REQUIRE(putResponse->status == k_status_internal_server_error);

    harness.stop();
    std::cerr.rdbuf(previousErrorBuffer);

    const std::string errorOutputText = capturedErrorOutput.str();
    REQUIRE(errorOutputText.find("publish_request_failed endpoint=/publish") != std::string::npos);
    REQUIRE(harness.resultCode() == 0);
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
