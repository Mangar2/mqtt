#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yaha/remote_service/remote_service_component.h"
#include "yaha/remote_service_http/remote_service_http_adapter.h"

namespace {

constexpr std::uint16_t kFallbackTestPort{28160U};
constexpr int kWaitAttempts{40};
constexpr int kWaitSleepMs{10};
constexpr int kHttpStatusOk{200};
constexpr int kHttpStatusNotFound{404};
constexpr double kNumericStateValue{23.0};

[[nodiscard]] std::uint16_t reserveFreeLocalPort() {
    httplib::Server probeServer;
    const int boundPort = probeServer.bind_to_any_port("127.0.0.1");
    if (boundPort <= 0) {
        return kFallbackTestPort;
    }

    const auto reservedPort = static_cast<std::uint16_t>(boundPort);
    probeServer.stop();
    return reservedPort;
}

bool waitForHttpServer(const std::uint16_t port) {
    httplib::Client client{"127.0.0.1", static_cast<int>(port)};
    for (int attempt = 0; attempt < kWaitAttempts; ++attempt) {
        if (const auto response = client.Get("/health")) {
            return response->status == kHttpStatusOk;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{kWaitSleepMs});
    }

    return false;
}

class FileStoreMappingMockServer {
public:
    explicit FileStoreMappingMockServer(std::uint16_t port, std::string mappingPath)
        : port_(port),
          mappingPath_(std::move(mappingPath)) {
        server_.Get("/health", [](const httplib::Request&, httplib::Response& response) {
            response.status = kHttpStatusOk;
            response.set_content("ok", "text/plain");
        });

        server_.Get(R"(.*)", [this](const httplib::Request& request, httplib::Response& response) {
            std::lock_guard<std::mutex> lock{stateMutex_};
            if (request.path != mappingPath_) {
                response.status = kHttpStatusNotFound;
                response.set_content("missing", "text/plain");
                return;
            }

            response.status = kHttpStatusOk;
            response.set_content(payloadText_, "application/json");
        });

        serverThread_ = std::thread([this]() {
            server_.listen("127.0.0.1", static_cast<int>(port_));
        });

        REQUIRE(waitForHttpServer(port_));
    }

    ~FileStoreMappingMockServer() {
        server_.stop();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    void setPayload(std::string payloadText) {
        std::lock_guard<std::mutex> lock{stateMutex_};
        payloadText_ = std::move(payloadText);
    }

private:
    std::uint16_t port_{0U};
    std::string mappingPath_;
    mutable std::mutex stateMutex_;
    httplib::Server server_;
    std::thread serverThread_;
    std::string payloadText_{R"({"services":[]})"};
};

} // namespace

TEST_CASE("remote_service_http_get_success_publishes_mapped_command", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setAccessTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-access";
    });

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handleGet(
        "/light",
        {
            {"deviceId", "kitchen"},
            {"state", "on"},
            {"accessToken", "valid-access"}});

    REQUIRE(response.statusCode == 200);
    REQUIRE(response.payload == "ok");
    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.size() == 1U);
        REQUIRE(published[0].topic() == "house/kitchen/light/set");
        REQUIRE(std::get<std::string>(published[0].value()) == "on");
    }

    component.close();
}

TEST_CASE("remote_service_http_post_success_publishes_mapped_command", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":"off","deviceToken":"valid-device"})");

    REQUIRE(response.statusCode == 200);
    REQUIRE(response.payload == "ok");
    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.size() == 1U);
        REQUIRE(published[0].topic() == "house/kitchen/light/set");
        REQUIRE(std::get<std::string>(published[0].value()) == "off");
    }

    component.close();
}

TEST_CASE("remote_service_http_get_unknown_path_returns_not_found", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setAccessTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-access";
    });
    component.setPublishCallback([](const yaha::Message&) {
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handleGet(
        "/unknown",
        {
            {"deviceId", "kitchen"},
            {"state", "on"},
            {"accessToken", "valid-access"}});

    REQUIRE(response.statusCode == 404);
    REQUIRE(response.payload == "Service not found");

    component.close();
}

TEST_CASE("remote_service_http_get_case_mismatch_returns_not_found", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/Light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setAccessTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-access";
    });
    component.setPublishCallback([](const yaha::Message&) {
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handleGet(
        "/light",
        {
            {"deviceId", "kitchen"},
            {"state", "on"},
            {"accessToken", "valid-access"}});

    REQUIRE(response.statusCode == 404);
    REQUIRE(response.payload == "Service not found");

    component.close();
}

TEST_CASE("remote_service_http_post_malformed_json_returns_bad_request", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":"on",)");

    REQUIRE(response.statusCode == 400);
    REQUIRE(response.payload == "Bad request");

    component.close();
}

TEST_CASE("remote_service_http_get_missing_token_returns_bad_request", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};

    const yaha::RemoteServiceHttpResponse response = adapter.handleGet(
        "/light",
        {
            {"deviceId", "kitchen"},
            {"state", "on"}});

    REQUIRE(response.statusCode == 400);
    REQUIRE(response.payload == "Bad request");

    component.close();
}

TEST_CASE("remote_service_http_get_empty_or_unvalidated_token_returns_bad_request", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};

    const yaha::RemoteServiceHttpResponse missingValidator = adapter.handleGet(
        "/light",
        {
            {"deviceId", "kitchen"},
            {"state", "on"},
            {"accessToken", "valid-access"}});
    REQUIRE(missingValidator.statusCode == 400);

    adapter.setAccessTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-access";
    });

    const yaha::RemoteServiceHttpResponse emptyToken = adapter.handleGet(
        "/light",
        {
            {"deviceId", "kitchen"},
            {"state", "on"},
            {"accessToken", ""}});
    REQUIRE(emptyToken.statusCode == 400);

    const yaha::RemoteServiceHttpResponse emptyDeviceId = adapter.handleGet(
        "/light",
        {
            {"deviceId", ""},
            {"state", "on"},
            {"accessToken", "valid-access"}});
    REQUIRE(emptyDeviceId.statusCode == 400);

    component.close();
}

TEST_CASE("remote_service_http_post_invalid_token_returns_bad_request", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":"on","deviceToken":"invalid"})");

    REQUIRE(response.statusCode == 400);
    REQUIRE(response.payload == "Bad request");

    component.close();
}

TEST_CASE("remote_service_http_post_requires_string_identity_fields", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    const yaha::RemoteServiceHttpResponse numericDeviceId = adapter.handlePost(
        "/light",
        R"({"deviceId":5,"state":"on","deviceToken":"valid-device"})");
    REQUIRE(numericDeviceId.statusCode == 400);

    const yaha::RemoteServiceHttpResponse numericDeviceToken = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":"on","deviceToken":7})");
    REQUIRE(numericDeviceToken.statusCode == 400);

    component.close();
}

TEST_CASE("remote_service_http_post_requires_all_required_fields", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    REQUIRE(adapter.handlePost("/light", R"({"state":"on","deviceToken":"valid-device"})").statusCode == 400);
    REQUIRE(adapter.handlePost("/light", R"({"deviceId":"kitchen","deviceToken":"valid-device"})").statusCode == 400);
    REQUIRE(adapter.handlePost("/light", R"({"deviceId":"kitchen","state":"on"})").statusCode == 400);

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("remote_service_http_post_accepts_numeric_and_boolean_state_tokens", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    const yaha::RemoteServiceHttpResponse numericState = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":23,"deviceToken":"valid-device"})");
    REQUIRE(numericState.statusCode == 200);

    const yaha::RemoteServiceHttpResponse boolState = adapter.handlePost(
        "/light",
        R"({"deviceId":"kitchen","state":true,"deviceToken":"valid-device"})");
    REQUIRE(boolState.statusCode == 200);

    std::lock_guard<std::mutex> lock{publishMutex};
    REQUIRE(published.size() == 2U);
    REQUIRE(std::holds_alternative<double>(published[0].value()));
    CHECK(std::get<double>(published[0].value()) == kNumericStateValue);
    REQUIRE(std::holds_alternative<std::string>(published[1].value()));
    CHECK(std::get<std::string>(published[1].value()) == "true");

    component.close();
}

TEST_CASE("remote_service_http_post_accepts_escaped_strings_and_whitespace", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid/device";
    });

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    const yaha::RemoteServiceHttpResponse response = adapter.handlePost(
        "/light",
        "  { \n \"deviceId\" : \"kitchen\" , \"state\" : \"line\\nvalue\\tend\" , \"deviceToken\" : \"valid\\/device\" }  ");

    REQUIRE(response.statusCode == 200);
    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.size() == 1U);
        REQUIRE(std::holds_alternative<std::string>(published[0].value()));
        CHECK(std::get<std::string>(published[0].value()) == "line\nvalue\tend");
    }

    component.close();
}

TEST_CASE("remote_service_http_post_invalid_escape_or_trailing_content_returns_bad_request", "[remote_service_http]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    yaha::RemoteServiceHttpAdapter adapter{component};
    adapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return tokenValue == "valid-device";
    });

    REQUIRE(adapter.handlePost(
                "/light",
                R"({"deviceId":"kitchen","state":"bad\q","deviceToken":"valid-device"})")
                .statusCode
            == 400);

    REQUIRE(adapter.handlePost(
                "/light",
                std::string{R"({"deviceId":"kitchen","state":"on","deviceToken":"valid-device"})"} + " trailing")
                .statusCode
            == 400);

    component.close();
}