#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yaha/automation_client/automation_client_component.h"

namespace {

constexpr std::uint16_t k_fallback_test_port{28111U};
constexpr int k_wait_attempts{40};
constexpr int k_wait_sleep_ms{10};
constexpr int k_http_ok_status{200};
constexpr int k_http_timeout_microseconds{500000};
constexpr int k_gate_delay_milliseconds{60};

[[nodiscard]] std::uint16_t reserveFreeLocalPort() {
    httplib::Server probeServer;
    const int boundPort = probeServer.bind_to_any_port("127.0.0.1");
    if (boundPort <= 0) {
        return k_fallback_test_port;
    }

    const auto portValue = static_cast<std::uint16_t>(boundPort);
    probeServer.stop();
    return portValue;
}

[[nodiscard]] bool waitForHttpServer(const std::uint16_t portValue) {
    httplib::Client client{"127.0.0.1", static_cast<int>(portValue)};
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
    for (int attemptValue = 0; attemptValue < k_wait_attempts; ++attemptValue) {
        if (const auto response = client.Get("/health")) {
            return response->status == k_http_ok_status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_wait_sleep_ms});
    }
    return false;
}

class FileStoreMockServer {
public:
    explicit FileStoreMockServer(const std::uint16_t portValue) {
        server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
            response.status = k_http_ok_status;
            response.set_content("ok", "text/plain");
            response.set_header("Connection", "close");
        });

        server_.Get("/automation/rules", [this](const httplib::Request&, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            response.status = k_http_ok_status;
            response.set_content(rulesJsonText_, "application/json");
            response.set_header("Connection", "close");
        });

        server_.Post("/automation/rules", [this](const httplib::Request& request, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            rulesJsonText_ = request.body;
            response.status = k_http_ok_status;
            response.set_content("", "text/plain");
            response.set_header("Connection", "close");
        });

        serverThread_ = std::thread([this, portValue]() {
            server_.listen("127.0.0.1", static_cast<int>(portValue));
        });

        REQUIRE(waitForHttpServer(portValue));
    }

    ~FileStoreMockServer() {
        server_.stop();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    void setRulesJson(std::string jsonText) {
        std::lock_guard<std::mutex> lock{mutex_};
        rulesJsonText_ = std::move(jsonText);
    }

private:
    std::mutex mutex_;
    httplib::Server server_;
    std::thread serverThread_;
    std::string rulesJsonText_{R"({"rules":{}})"};
};

} // namespace

TEST_CASE("automation_component_management_update_accepts_topic_object_map", "[automation_client]") {
    const std::uint16_t portValue = reserveFreeLocalPort();
    FileStoreMockServer fileStore{portValue};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = portValue;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{R"({"topic":{"house/light/set":"on","house/pump/set":"off"}})"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "updated");

    component.close();
}

TEST_CASE("automation_component_skips_rule_with_active_false", "[automation_client]") {
    const std::uint16_t portValue = reserveFreeLocalPort();
    FileStoreMockServer fileStore{portValue};
    fileStore.setRulesJson(
        R"({"rules":{"pumpRule":{"active":false,"topic":"house/pump/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = portValue;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/presence/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.empty());

    component.close();
}

TEST_CASE("automation_component_event_gate_any_of_requires_matching_recent_event", "[automation_client]") {
    const std::uint16_t portValue = reserveFreeLocalPort();
    FileStoreMockServer fileStore{portValue};
    fileStore.setRulesJson(
        R"({"rules":{"anyOfRule":{"topic":"house/light/set","anyOf":["house/event/allowed"],"check":"1","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = portValue;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/event/blocked",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});
    component.handleMessage(yaha::Message{
        "house/event/allowed",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    const auto outputCount = static_cast<std::size_t>(std::count_if(
        published.begin(),
        published.end(),
        [](const yaha::Message& message) {
            return message.topic() == "house/light/set";
        }));
    REQUIRE(outputCount == 1U);

    component.close();
}

TEST_CASE("automation_component_delay_and_cooldown_control_repeated_outputs", "[automation_client]") {
    const std::uint16_t portValue = reserveFreeLocalPort();
    FileStoreMockServer fileStore{portValue};
    fileStore.setRulesJson(
        R"({"rules":{"timedRule":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on","delayInSeconds":0.05,"cooldownInSeconds":0.05}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = portValue;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{"$MONITOR/presence/set", std::string{"on"}, yaha::Qos::AtLeastOnce, false});
    component.handleMessage(yaha::Message{"$MONITOR/presence/set", std::string{"on"}, yaha::Qos::AtLeastOnce, false});
    std::this_thread::sleep_for(std::chrono::milliseconds{k_gate_delay_milliseconds});
    component.handleMessage(yaha::Message{"$MONITOR/presence/set", std::string{"on"}, yaha::Qos::AtLeastOnce, false});
    std::this_thread::sleep_for(std::chrono::milliseconds{k_gate_delay_milliseconds});
    component.handleMessage(yaha::Message{"$MONITOR/presence/set", std::string{"on"}, yaha::Qos::AtLeastOnce, false});

    std::lock_guard<std::mutex> lock{publishMutex};
    const auto outputCount = static_cast<std::size_t>(std::count_if(
        published.begin(),
        published.end(),
        [](const yaha::Message& message) {
            return message.topic() == "house/light/set";
        }));
    REQUIRE(outputCount >= 2U);

    component.close();
}
