#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yaha/value_service/value_service_component.h"

namespace {

constexpr std::uint16_t k_fallback_test_port{28120U};
constexpr int k_wait_attempts{40};
constexpr int k_http_ok_status{200};
constexpr int k_http_internal_server_error_status{500};
constexpr int k_wait_sleep_ms{10};
constexpr double k_non_integral_test_value{21.5};
constexpr double k_integral_test_value{21.0};

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
        });

        server_.Get("/valueservice/values", [this](const httplib::Request&, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            response.status = k_http_ok_status;
            response.set_content(valuesJsonText_, "application/json");
        });

        server_.Post("/valueservice/values", [this](const httplib::Request& request, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{mutex_};
            lastPostedBodyText_ = request.body;
            postCountValue_ += 1U;
            if (postStatus_ == k_http_ok_status) {
                valuesJsonText_ = request.body;
            }
            response.status = postStatus_;
            response.set_content("", "text/plain");
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

    void setValuesJson(std::string jsonText) {
        std::lock_guard<std::mutex> lock{mutex_};
        valuesJsonText_ = std::move(jsonText);
    }

    [[nodiscard]] std::string lastPostedBody() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return lastPostedBodyText_;
    }

    [[nodiscard]] std::uint32_t postCount() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return postCountValue_;
    }

    void setPostStatus(const int status) {
        std::lock_guard<std::mutex> lock{mutex_};
        postStatus_ = status;
    }

private:
    std::uint16_t port_{0U};
    mutable std::mutex mutex_;
    httplib::Server server_;
    std::thread serverThread_;
    std::string valuesJsonText_{"{}"};
    std::string lastPostedBodyText_;
    std::uint32_t postCountValue_{0U};
    int postStatus_{k_http_ok_status};
};

} // namespace

TEST_CASE("value_service_run_loads_values_and_publishes_replay", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/light\":\"on\",\"house/temperature\":21}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.valueCount() == 2U);

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.size() == 2U);
    REQUIRE(published[0].retain());
    REQUIRE(published[1].retain());

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("value_service_set_updates_map_publishes_and_persists", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/light/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    const auto value = component.valueForKey("house/light");
    REQUIRE(value.has_value());
    REQUIRE(std::holds_alternative<std::string>(*value));
    REQUIRE(std::get<std::string>(*value) == "on");

    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.empty() == false);
        REQUIRE(published.back().topic() == "house/light");
        REQUIRE(published.back().retain());
    }

    REQUIRE(fileStore.postCount() >= 1U);
    REQUIRE(fileStore.lastPostedBody().find("\"house/light\":\"on\"") != std::string::npos);

    component.close();
}

TEST_CASE("value_service_monitoring_reload_replaces_values_and_replays", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/light\":\"on\"}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.run();

    fileStore.setValuesJson("{\"house/heating\":\"off\"}");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{"{\"keyPath\":\"/valueservice/values\",\"changeType\":\"changed\"}"},
        yaha::Qos::AtLeastOnce,
        false});

    const auto oldValue = component.valueForKey("house/light");
    const auto newValue = component.valueForKey("house/heating");
    REQUIRE_FALSE(oldValue.has_value());
    REQUIRE(newValue.has_value());

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.size() >= 2U);
    REQUIRE(published.back().topic() == "house/heating");

    component.close();
}

TEST_CASE("value_service_set_publishes_when_persist_fails", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setPostStatus(k_http_internal_server_error_status);

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/light/set",
        std::string{"on"},
        yaha::Qos::AtLeastOnce,
        false});

    const auto value = component.valueForKey("house/light");
    REQUIRE(value.has_value());

    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE_FALSE(published.empty());
        REQUIRE(published.back().topic() == "house/light");
        REQUIRE(published.back().retain());
    }

    REQUIRE(fileStore.postCount() >= 1U);
    component.close();
}

TEST_CASE("value_service_monitoring_non_matching_keypath_does_not_reload", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/light\":\"on\"}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.run();
    const std::size_t startupPublishCount = [&publishMutex, &published]() {
        std::lock_guard<std::mutex> lock{publishMutex};
        return published.size();
    }();

    fileStore.setValuesJson("{\"house/heating\":\"off\"}");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{"{\"keyPath\":\"/other/path\",\"changeType\":\"changed\"}"},
        yaha::Qos::AtLeastOnce,
        false});

    const auto oldValue = component.valueForKey("house/light");
    const auto newValue = component.valueForKey("house/heating");
    REQUIRE(oldValue.has_value());
    REQUIRE_FALSE(newValue.has_value());

    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.size() == startupPublishCount);
    }

    component.close();
}

TEST_CASE("value_service_rejects_non_integral_numeric_set_value", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/temp/set",
        k_non_integral_test_value,
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.valueCount() == 0U);
    REQUIRE(fileStore.postCount() == 0U);

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.empty());

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("value_service_accepts_integral_double_and_persists_number", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{
        "house/temp/set",
        k_integral_test_value,
        yaha::Qos::AtLeastOnce,
        false});

    const auto value = component.valueForKey("house/temp");
    REQUIRE(value.has_value());
    REQUIRE(std::holds_alternative<double>(*value));
    REQUIRE(std::get<double>(*value) == k_integral_test_value);
    REQUIRE(fileStore.postCount() >= 1U);
    REQUIRE(fileStore.lastPostedBody().find("\"house/temp\":21") != std::string::npos);

    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE_FALSE(published.empty());
        REQUIRE(published.back().topic() == "house/temp");
    }

    component.close();
}

TEST_CASE("value_service_monitoring_non_string_payload_is_ignored", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/light\":\"on\"}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        1.0,
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.valueForKey("house/light").has_value());
    REQUIRE_FALSE(component.valueForKey("house/heating").has_value());

    component.close();
}

TEST_CASE("value_service_handles_escaped_json_strings_and_idempotent_run", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/text\":\"line\\n\\\"quoted\\\"\\tvalue\"}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();
    component.run();

    const auto loadedValue = component.valueForKey("house/text");
    REQUIRE(loadedValue.has_value());
    REQUIRE(std::holds_alternative<std::string>(*loadedValue));
    REQUIRE(std::get<std::string>(*loadedValue).find("quoted") != std::string::npos);

    component.handleMessage(yaha::Message{
        "house/text/set",
        std::string{"a\\b\"c\n"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(fileStore.lastPostedBody().find("\\\\") != std::string::npos);
    REQUIRE(fileStore.lastPostedBody().find("\\\"") != std::string::npos);
    REQUIRE(fileStore.lastPostedBody().find("\\n") != std::string::npos);

    component.close();
}

TEST_CASE("value_service_get_subscriptions_contains_monitor_and_set_topics", "[value_service]") {
    const std::uint16_t port = reserveFreeLocalPort();
    FileStoreMockServer fileStore{port};
    fileStore.setValuesJson("{\"house/light\":\"on\",\"house/heating\":\"off\"}");

    yaha::ValueServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = port;

    yaha::ValueServiceComponent component{config};
    component.run();

    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();
    REQUIRE(subscriptions.contains("$MONITOR/FileStore/#"));
    REQUIRE(subscriptions.contains("house/light/set"));
    REQUIRE(subscriptions.contains("house/heating/set"));

    component.close();
}
