#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "yaha/automation_client/automation_client_component.h"

namespace {

constexpr std::uint16_t k_fallback_test_port{28110U};
constexpr int k_wait_attempts{40};
constexpr int k_http_ok_status{200};
constexpr int k_wait_sleep_ms{10};
constexpr int k_http_timeout_microseconds{500000};
constexpr int k_http_error_status{500};
constexpr std::size_t k_retry_trigger_messages{5U};

void configureHttpClientTimeouts(httplib::Client& client) {
    client.set_connection_timeout(0, k_http_timeout_microseconds);
    client.set_read_timeout(0, k_http_timeout_microseconds);
}

void forceCloseConnection(httplib::Response& response) {
    response.set_header("Connection", "close");
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
            return response->status == k_http_ok_status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{k_wait_sleep_ms});
    }
    return false;
}

class FileStoreMockServer {
public:
    explicit FileStoreMockServer(const std::uint16_t port)
        : port_(port) {
        server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
            response.status = k_http_ok_status;
            response.set_content("ok", "text/plain");
            forceCloseConnection(response);
        });

        server_.Get("/automation/rules", [this](const httplib::Request&, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            response.status = getStatusCode_;
            response.set_content(rulesJsonText_, "application/json");
            forceCloseConnection(response);
        });

        server_.Post("/automation/rules", [this](const httplib::Request& request, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            lastPostedBodyText_ = request.body;
            postCountValue_ += 1U;
            response.status = postStatusCode_;
            if (postStatusCode_ == k_http_ok_status) {
                rulesJsonText_ = request.body;
            }
            response.set_content("", "text/plain");
            forceCloseConnection(response);
        });

        serverThread_ = std::thread([this]() {
            server_.listen("127.0.0.1", static_cast<int>(port_));
        });

        REQUIRE(waitForHttpServer(port_));
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

    [[nodiscard]] std::string lastPostedBody() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return lastPostedBodyText_;
    }

    [[nodiscard]] std::uint32_t postCount() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return postCountValue_;
    }

    void setPostStatus(const int statusCode) {
        std::lock_guard<std::mutex> lock{mutex_};
        postStatusCode_ = statusCode;
    }

    void setGetStatus(const int statusCode) {
        std::lock_guard<std::mutex> lock{mutex_};
        getStatusCode_ = statusCode;
    }

private:
    std::uint16_t port_{0U};
    mutable std::mutex mutex_;
    httplib::Server server_;
    std::thread serverThread_;
    std::string rulesJsonText_{R"({"rules":{}})"};
    std::string lastPostedBodyText_;
    std::uint32_t postCountValue_{0U};
    int getStatusCode_{k_http_ok_status};
    int postStatusCode_{k_http_ok_status};
};

} // namespace

TEST_CASE("automation_component_run_loads_rules_from_filestore", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"wake":{"topic":"house/light","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.ruleCount() == 1U);
    REQUIRE(component.hasRule("wake"));

    component.close();
}

TEST_CASE("automation_component_monitoring_event_reload_rules", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"first":{"topic":"house/light","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("first"));

    fileStore.setRulesJson(R"({"rules":{"second":{"topic":"house/heating","value":"off"}}})");

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"({"keyPath":"/automation/rules","changeType":"changed"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.ruleCount() == 1U);
    REQUIRE_FALSE(component.hasRule("first"));
    REQUIRE(component.hasRule("second"));

    component.close();
}

TEST_CASE("automation_component_management_update_persists_and_acks", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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
        std::string{R"({"topic":"house/light/set","value":"on"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("demo"));
    REQUIRE(fileStore.postCount() >= 1U);
    REQUIRE(fileStore.lastPostedBody().find("\"demo\"") != std::string::npos);

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    const auto& payloadText = std::get<std::string>(published.back().value());
    REQUIRE(payloadText.find("\"isValid\":true") != std::string::npos);
    REQUIRE(payloadText.find("\"name\":\"demo\"") != std::string::npos);

    component.close();
}

TEST_CASE("automation_component_evaluates_rules_and_publishes_outputs", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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
    REQUIRE(published.empty() == false);
    REQUIRE(published.back().topic() == "house/light/set");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "on");

    component.close();
}

TEST_CASE("automation_component_bootstraps_status_presence_initial", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"init":{"topic":"house/init/set","check":"status/presence = initial","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/event/trigger",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "house/init/set");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "on");

    component.close();
}

TEST_CASE("automation_component_management_delete_persists_and_acks_deleted", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"demo":{"topic":"house/light/set","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("demo"));

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{"delete"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE_FALSE(component.hasRule("demo"));

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "deleted");

    component.close();
}

TEST_CASE("automation_component_management_non_string_payload_acks_invalid", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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
        1.0,
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "validation_failed");

    component.close();
}

TEST_CASE("automation_component_management_invalid_rule_persists_and_publishes_isvalid_false", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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
        std::string{R"({"value":"on"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("demo"));
    REQUIRE(fileStore.postCount() >= 1U);

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.size() >= 2U);
    REQUIRE(published[published.size() - 2U].topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published[published.size() - 2U].value()));
    const auto& invalidRulePayload = std::get<std::string>(published[published.size() - 2U].value());
    REQUIRE(invalidRulePayload.find("\"isValid\":false") != std::string::npos);
    REQUIRE(invalidRulePayload.find("\"errors\":[") != std::string::npos);
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "invalid rule");

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("automation_component_management_update_persist_failure_rolls_back_and_acks", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"stable":{"topic":"house/light/set","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("stable"));
    REQUIRE_FALSE(component.hasRule("demo"));

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    fileStore.setPostStatus(k_http_error_status);
    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{R"({"topic":"house/heating/set","value":"off"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("stable"));
    REQUIRE_FALSE(component.hasRule("demo"));

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "persist_failed");

    component.close();
}

TEST_CASE("automation_component_management_delete_persist_failure_rolls_back_and_acks", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"demo":{"topic":"house/light/set","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("demo"));

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    fileStore.setPostStatus(k_http_error_status);
    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{"delete"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("demo"));

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "persist_failed");

    component.close();
}

TEST_CASE("automation_component_publish_failure_logs_out_fail_without_false_out_success", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;
    config.logOutgoingMessages = true;

    yaha::AutomationClientComponent component{config};
    component.run();
    component.setPublishCallback([](const yaha::Message&) {
        throw std::runtime_error{"publish failed"};
    });

    std::ostringstream capturedOutput{};
    std::streambuf* previousStdoutBuffer = std::cout.rdbuf(capturedOutput.rdbuf());
    std::streambuf* previousStderrBuffer = std::cerr.rdbuf(capturedOutput.rdbuf());

    component.handleMessage(yaha::Message{
        "$MONITOR/presence/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    std::cout.rdbuf(previousStdoutBuffer);
    std::cerr.rdbuf(previousStderrBuffer);

    const std::string logOutput = capturedOutput.str();
    REQUIRE(logOutput.find("automation_client[out-fail] topic=house/light/set") != std::string::npos);
    REQUIRE(logOutput.find("automation_client[out] topic=house/light/set") == std::string::npos);

    component.close();
}

TEST_CASE("automation_component_retries_management_ack_after_transient_publish_failure", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    std::size_t publishAttemptCount{0U};
    component.setPublishCallback([&publishMutex, &published, &publishAttemptCount](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        publishAttemptCount += 1U;
        if (publishAttemptCount == 1U) {
            return yaha::PublishResult::fail(yaha::PublishFailureCategory::AckTimeout,
                                             "timed out waiting for PUBACK from broker");
        }
        published.push_back(message.clone());
        return yaha::PublishResult::ok();
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        1.0,
        yaha::Qos::AtLeastOnce,
        false});

    component.handleMessage(yaha::Message{
        "house/noop/topic",
        std::string{"noop"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "validation_failed");

    component.close();
}

TEST_CASE("automation_component_retry_budget_exhaustion_logs_failure", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    component.setPublishCallback([](const yaha::Message&) {
        throw std::runtime_error{"publish failure"};
    });

    std::ostringstream capturedOutput{};
    std::streambuf* previousStdoutBuffer = std::cout.rdbuf(capturedOutput.rdbuf());
    std::streambuf* previousStderrBuffer = std::cerr.rdbuf(capturedOutput.rdbuf());

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        1.0,
        yaha::Qos::AtLeastOnce,
        false});

    for (std::size_t attemptIndex = 0U; attemptIndex < k_retry_trigger_messages; ++attemptIndex) {
        component.handleMessage(yaha::Message{
            "house/noop/topic",
            std::string{"noop"},
            yaha::Qos::AtLeastOnce,
            false});
    }

    std::cout.rdbuf(previousStdoutBuffer);
    std::cerr.rdbuf(previousStderrBuffer);

    const std::string logOutput = capturedOutput.str();
    REQUIRE(logOutput.find("category=retry_exhausted") != std::string::npos);

    component.close();
}

TEST_CASE("automation_component_retries_rule_output_after_publish_result_failure", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    std::size_t outputPublishAttempts{0U};
    component.setPublishCallback([&publishMutex, &published, &outputPublishAttempts](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        if (message.topic() == "house/light/set") {
            outputPublishAttempts += 1U;
            if (outputPublishAttempts == 1U) {
                return yaha::PublishResult::fail(yaha::PublishFailureCategory::AckTimeout,
                                                 "timed out waiting for PUBACK from broker");
            }
        }

        published.push_back(message.clone());
        return yaha::PublishResult::ok();
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/presence/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    component.handleMessage(yaha::Message{
        "house/retry/trigger",
        std::string{"trigger"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(outputPublishAttempts >= 2U);
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "house/light/set");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "on");

    component.close();
}

TEST_CASE("automation_component_run_with_filestore_get_failure_keeps_empty_rules", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setGetStatus(k_http_error_status);

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.ruleCount() == 0U);

    component.close();
}

TEST_CASE("automation_component_monitor_reload_get_failure_keeps_existing_rules", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"first":{"topic":"house/light","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("first"));

    fileStore.setGetStatus(k_http_error_status);
    fileStore.setRulesJson(R"({"rules":{"second":{"topic":"house/heating","value":"off"}}})");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"({"keyPath":"/automation/rules","changeType":"changed"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("first"));
    REQUIRE_FALSE(component.hasRule("second"));

    component.close();
}

TEST_CASE("automation_component_flushes_retry_queue_after_callback_restore", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        1.0,
        yaha::Qos::AtLeastOnce,
        false});

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/retry/trigger",
        std::string{"trigger"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/demo");

    component.close();
}

TEST_CASE("automation_component_monitoring_deleted_does_not_reload", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(R"({"rules":{"first":{"topic":"house/light","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();
    REQUIRE(component.hasRule("first"));

    fileStore.setRulesJson(R"({"rules":{"second":{"topic":"house/heating","value":"off"}}})");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/deleted",
        std::string{R"({"keyPath":"/automation/rules","changeType":"deleted"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("first"));
    REQUIRE_FALSE(component.hasRule("second"));

    component.close();
}

TEST_CASE("automation_component_recovers_from_non_object_rules_payload", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson("[]");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.ruleCount() == 0U);
    REQUIRE_FALSE(component.hasRule("demo"));

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/demo/set",
        std::string{R"({"topic":"house/light/set","value":"on","enabled":true,"weight":1.5,"list":[1,2]})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasRule("demo"));
    REQUIRE(fileStore.postCount() >= 1U);

    component.close();
}

TEST_CASE("automation_component_get_subscriptions_includes_dynamic_topics", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"withVar":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();
    REQUIRE(subscriptions.contains("$MONITOR/presence/set"));
    REQUIRE(subscriptions.contains("$MONITOR/automation/#"));

    component.close();
}

TEST_CASE("automation_component_logs_incoming_and_outgoing_messages_when_enabled", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;
    config.logIncomingMessages = true;
    config.logOutgoingMessages = true;

    yaha::AutomationClientComponent component{config};
    component.run();
    component.setPublishCallback([](const yaha::Message&) {
    });

    std::ostringstream capturedOutput;
    std::streambuf* previousBuffer = std::cout.rdbuf(capturedOutput.rdbuf());

    component.handleMessage(yaha::Message{
        "$MONITOR/presence/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    std::cout.rdbuf(previousBuffer);

    const std::string logOutput = capturedOutput.str();
    REQUIRE(logOutput.find("automation_client[in] topic=$MONITOR/presence/set") != std::string::npos);
    REQUIRE(logOutput.find("automation_client[out] topic=house/light/set") != std::string::npos);
    REQUIRE(logOutput.find("Rule: presenceOn") != std::string::npos);

    component.close();
}

TEST_CASE("automation_component_debug_trace_request_reports_not_triggered", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"0","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/presenceOn/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/presenceOn/trace");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    const std::string traceValue = std::get<std::string>(published.back().value());
    REQUIRE(traceValue == "not_triggered");
    REQUIRE_FALSE(published.back().reason().empty());
    const bool hasNoOutboundTrace = std::ranges::any_of(
        published.back().reason(),
        [](const yaha::ReasonEntry& reasonEntry) {
            return reasonEntry.message.find("debug:result no outbound message") != std::string::npos;
        });
    REQUIRE(hasNoOutboundTrace);

    component.close();
}

TEST_CASE("automation_component_debug_trace_request_reports_missing_rule", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/missing/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/missing/trace");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "error");
    REQUIRE_FALSE(published.back().reason().empty());
    const bool hasLookupFailureTrace = std::ranges::any_of(
        published.back().reason(),
        [](const yaha::ReasonEntry& reasonEntry) {
            return reasonEntry.message.find("rule lookup failed") != std::string::npos;
        });
    REQUIRE(hasLookupFailureTrace);

    component.close();
}

TEST_CASE("automation_component_debug_trace_request_resolves_hierarchical_rule_link", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"ground":{"livingroom":{"roller":{"rules":{"UpMorning":{"topic":"house/roller/set","check":"1","value":"up"}}}}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::AutomationClientComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/ground/livingroom/roller/UpMorning/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/ground/livingroom/roller/UpMorning/trace");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "triggered");

    const bool hasRulePathTrace = std::ranges::any_of(
        published.back().reason(),
        [](const yaha::ReasonEntry& reasonEntry) {
            return reasonEntry.message.find("debug:rule path=") != std::string::npos;
        });
    REQUIRE(hasRulePathTrace);

    component.close();
}

TEST_CASE("automation_component_debug_trace_request_reports_delivery_control_suppression", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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
    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/presenceOn/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/presenceOn/trace");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "triggered");

    const bool hasSuppressionTrace = std::ranges::any_of(
        published.back().reason(),
        [](const yaha::ReasonEntry& reasonEntry) {
            return reasonEntry.message.find("would send=0") != std::string::npos
                && reasonEntry.message.find("delivery controls suppress: dedup/delay/cooldown") != std::string::npos;
        });
    REQUIRE(hasSuppressionTrace);

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("automation_component_debug_trace_raw_payload_escapes_multiline_reason_entries", "[automation_client]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setRulesJson(
        R"({"rules":{"presenceOn":{"topic":"house/light/set","check":"map_1 = (on: awake, default: absent)\n$MONITOR/presence/set = on","value":"on"}}})");

    yaha::AutomationClientConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

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

    component.handleMessage(yaha::Message{
        "$MONITOR/automation/rules/presenceOn/debug",
        std::string{"1"},
        yaha::Qos::AtLeastOnce,
        false});

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().topic() == "$MONITOR/automation/rules/presenceOn/trace");
    REQUIRE(std::holds_alternative<std::string>(published.back().value()));
    REQUIRE(std::get<std::string>(published.back().value()) == "triggered");

    const std::optional<std::string>& rawPayload = published.back().rawPayload();
    REQUIRE(rawPayload.has_value());
    REQUIRE(rawPayload->find('\n') == std::string::npos);
    REQUIRE(rawPayload->find("debug:explain Rule:") != std::string::npos);
    REQUIRE(rawPayload->find("presenceOn") != std::string::npos);

    REQUIRE_FALSE(published.back().reason().empty());
    const bool hasExplainTrace = std::ranges::any_of(
        published.back().reason(),
        [](const yaha::ReasonEntry& reasonEntry) {
            return reasonEntry.message.find("debug:explain Rule:") != std::string::npos
                && reasonEntry.message.find("presenceOn") != std::string::npos;
        });
    REQUIRE(hasExplainTrace);

    component.close();
}
