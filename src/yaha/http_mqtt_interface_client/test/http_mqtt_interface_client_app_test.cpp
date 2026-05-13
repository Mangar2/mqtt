#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

#include "httplib.h"

#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/ini/ini_document.h"

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

void verifyHealthEndpoint(httplib::Client& client) {
    const auto healthResponse = client.Get("/health");
    REQUIRE(healthResponse != nullptr);
    REQUIRE(healthResponse->status == k_status_ok);
}

void verifyCorsHeaders(const httplib::Result& response) {
    REQUIRE(response != nullptr);
    REQUIRE(response->get_header_value("Access-Control-Allow-Origin") == "*");
    REQUIRE(response->get_header_value("Access-Control-Allow-Methods") == k_expected_cors_methods);
    REQUIRE(response->get_header_value("Access-Control-Allow-Headers") == k_expected_cors_headers);
}

void verifyOptionsEndpoint(httplib::Client& client, const char* endpointPath) {
    const auto optionsResponse = client.Options(endpointPath);
    REQUIRE(optionsResponse != nullptr);
    REQUIRE(optionsResponse->status == k_status_no_content);
    verifyCorsHeaders(optionsResponse);
    REQUIRE(optionsResponse->get_header_value("Access-Control-Max-Age") == "86400");
}

void verifyOptionsEndpoints(httplib::Client& client) {
    verifyOptionsEndpoint(client, "/publish");
    verifyOptionsEndpoint(client, "/publish.php");
    verifyOptionsEndpoint(client, "/pubrel");
}

void verifyPutEndpoints(httplib::Client& client) {
    const httplib::Headers publishHeaders{
        {"version", "1.0"},
        {"qos", "1"},
        {"retain", "0"},
        {"packetid", "7"},
    };
    const auto publishResponse = client.Put("/publish", publishHeaders, "{}", "application/json");
    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_status_no_content);
    verifyCorsHeaders(publishResponse);

    const httplib::Headers pubrelHeaders{{"version", "1.0"}, {"packetid", "7"}};
    const auto pubrelResponse = client.Put("/pubrel", pubrelHeaders, "{}", "application/json");
    REQUIRE(pubrelResponse != nullptr);
    REQUIRE(pubrelResponse->status == k_status_no_content);
    verifyCorsHeaders(pubrelResponse);
}

void verifyPostPublishEndpoint(httplib::Client& client, const httplib::Params& formParams) {
    const auto postFormResponse = client.Post("/publish", formParams);
    REQUIRE(postFormResponse != nullptr);
    REQUIRE(postFormResponse->status == k_status_no_content);
    verifyCorsHeaders(postFormResponse);

    const std::string jsonBody =
        "{"
        "\"topic\":\"lab%2Fstate\","
        "\"value\":true,"
        "\"reason\":[{\"message\":\"a\\n\",\"timestamp\":\"2024-01-01T00:00:00Z\"}],"
        "\"qos\":2,"
        "\"retain\":true"
        "}";
    const httplib::Headers jsonHeaders{{"content-type", "application/json"}, {"token", "tok-json"}};
    const auto postJsonResponse = client.Post("/publish", jsonHeaders, jsonBody, "application/json");
    REQUIRE(postJsonResponse != nullptr);
    REQUIRE(postJsonResponse->status == k_status_no_content);
    verifyCorsHeaders(postJsonResponse);
}

void verifyPostPublishPhpEndpoint(httplib::Client& client, const httplib::Params& formParams) {
    const auto postPhpResponse = client.Post("/publish.php", formParams);
    REQUIRE(postPhpResponse != nullptr);
    REQUIRE(postPhpResponse->status == k_status_no_content);
    verifyCorsHeaders(postPhpResponse);
}

void exerciseHttpServerEndpoints(const std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    verifyHealthEndpoint(client);
    verifyPutEndpoints(client);
    verifyOptionsEndpoints(client);

    const httplib::Params formParams{{"topic", "sensor%2Ftemp"}, {"value", "42"}, {"token", "tok-form"}};
    verifyPostPublishEndpoint(client, formParams);
    verifyPostPublishPhpEndpoint(client, formParams);
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
TEST_CASE("run_http_mqtt_interface_client_serves_endpoints_logs_publish_and_stops_on_signal", "[http_mqtt_interface_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    std::ostringstream capturedOutput{};
    std::streambuf* previousOutputBuffer = std::cout.rdbuf(capturedOutput.rdbuf());
    std::mutex forwardedMessagesMutex{};
    std::vector<yaha::Message> forwardedMessages{};

    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "127.0.0.1";
    config.listenerPort = port;
    config.enablePublishPhpAlias = true;
    config.useLegacyPhpResponse = false;

    int exitCode = -1;
    std::thread serverThread([&config, &exitCode, &forwardedMessagesMutex, &forwardedMessages]() {
        exitCode = yaha::runHttpMqttInterfaceClient(
            config,
            [&forwardedMessagesMutex, &forwardedMessages](const yaha::Message& message) {
                std::lock_guard<std::mutex> lock{forwardedMessagesMutex};
                forwardedMessages.push_back(message.clone());
            });
    });

    REQUIRE(waitForHttpServer(port));

    exerciseHttpServerEndpoints(port);

    std::raise(SIGTERM);
    serverThread.join();
    std::cout.rdbuf(previousOutputBuffer);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("http_mqtt_interface_client[in] method=PUT endpoint=/publish version=1.0") !=
        std::string::npos);
    REQUIRE(outputText.find("http_mqtt_interface_client[in] method=POST endpoint=/publish") !=
        std::string::npos);
    REQUIRE(outputText.find("http_mqtt_interface_client[in] method=POST endpoint=/publish.php") !=
        std::string::npos);
    REQUIRE(outputText.find("http_mqtt_interface_client[out] broker_publish_ack topic=sensor/temp") !=
        std::string::npos);
    {
        std::lock_guard<std::mutex> lock{forwardedMessagesMutex};
        REQUIRE(forwardedMessages.empty() == false);
        REQUIRE(forwardedMessages.front().topic() == "sensor/temp");
    }
    REQUIRE(exitCode == 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("run_http_mqtt_interface_client_logs_broker_publish_error_when_ack_missing", "[http_mqtt_interface_client]") {
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

    int exitCode = -1;
    std::thread serverThread([&config, &exitCode]() {
        exitCode = yaha::runHttpMqttInterfaceClient(
            config,
            [](const yaha::Message&) {
                throw std::runtime_error{"timed out waiting for PUBACK from broker"};
            });
    });

    REQUIRE(waitForHttpServer(port));

    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    configureHttpClientTimeouts(client);
    const httplib::Params formParams{{"topic", "sensor%2Ftemp"}, {"value", "42"}, {"token", "tok-form"}};
    const auto postResponse = client.Post("/publish", formParams);
    REQUIRE(postResponse != nullptr);
    REQUIRE(postResponse->status == 500);

    std::raise(SIGTERM);
    serverThread.join();
    std::cout.rdbuf(previousOutputBuffer);
    std::cerr.rdbuf(previousErrorBuffer);

    const std::string outputText = capturedOutput.str();
    REQUIRE(outputText.find("http_mqtt_interface_client[error] broker_publish_failed") !=
        std::string::npos);
    REQUIRE(outputText.find("topic=sensor/temp") != std::string::npos);
    REQUIRE(outputText.find("value=42") != std::string::npos);
    REQUIRE(outputText.find("detail=message_was_sent_but_broker_reported_no_ack") !=
        std::string::npos);

    const std::string errorOutputText = capturedErrorOutput.str();
    REQUIRE(errorOutputText.find("http_mqtt_interface_client[error] publish_request_failed endpoint=/publish") !=
        std::string::npos);
    REQUIRE(errorOutputText.find("timed out waiting for PUBACK from broker") != std::string::npos);
    REQUIRE(exitCode == 0);
}

TEST_CASE("run_http_mqtt_interface_client_returns_error_on_listen_failure", "[http_mqtt_interface_client]") {
    yaha::HttpMqttInterfaceClientConfig config{};
    config.listenerHost = "invalid.invalid.invalid";
    config.listenerPort = reserveFreeLocalPort();

    const int exitCode = yaha::runHttpMqttInterfaceClient(config);
    REQUIRE(exitCode == 1);
}
