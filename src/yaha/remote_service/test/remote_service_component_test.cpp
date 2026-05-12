#include <catch2/catch_test_macros.hpp>

#include "httplib.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "yaha/remote_service/remote_service_component.h"

namespace {

constexpr std::uint16_t kFallbackTestPort{28140U};
constexpr int kWaitAttempts{40};
constexpr int kWaitSleepMs{10};
constexpr int kHttpStatusOk{200};
constexpr int kHttpStatusNotFound{404};
constexpr int kHttpStatusInternalServerError{500};

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

            getCount_ += 1U;
            response.status = getStatus_;
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

    void setStatus(int statusCode) {
        std::lock_guard<std::mutex> lock{stateMutex_};
        getStatus_ = statusCode;
    }

    [[nodiscard]] std::uint32_t getCount() const {
        std::lock_guard<std::mutex> lock{stateMutex_};
        return getCount_;
    }

private:
    std::uint16_t port_{0U};
    std::string mappingPath_;
    mutable std::mutex stateMutex_;
    httplib::Server server_;
    std::thread serverThread_;
    std::string payloadText_{R"({"services":[]})"};
    int getStatus_{kHttpStatusOk};
    std::uint32_t getCount_{0U};
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("remote_service_mapping_payload_parser_accepts_valid_services", "[remote_service]") {
    const std::string payloadText =
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"},"qos":2,"reason":"switch"}]})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(parsed.size() == 1U);
    REQUIRE(parsed.contains("/light"));
    REQUIRE(parsed.at("/light").devices.at("kitchen") == "house/kitchen/light/set");
    REQUIRE(parsed.at("/light").qos == yaha::Qos::ExactlyOnce);
    REQUIRE(parsed.at("/light").reason == "switch");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_invalid_payload_without_partial_apply", "[remote_service]") {
    const std::string payloadText =
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}},{"path":"/heating"}]})";

    yaha::RemoteServiceMap parsed{};
    parsed.insert({"/kept", yaha::RemoteServiceServiceMapping{}});

    std::string errorMessage{};
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage.find("service.devices") != std::string::npos);
    REQUIRE(parsed.size() == 1U);
    REQUIRE(parsed.contains("/kept"));
}

TEST_CASE("remote_service_mapping_payload_parser_duplicate_path_keeps_first_and_writes_cerr", "[remote_service]") {
    const std::string payloadText =
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic/first"}},{"path":"/light","devices":{"kitchen":"topic/second"}}]})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    std::ostringstream errorStream{};
    auto* previousBuffer = std::cerr.rdbuf(errorStream.rdbuf());
    const bool success = yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage);
    std::cerr.rdbuf(previousBuffer);

    REQUIRE(success);
    REQUIRE(parsed.size() == 1U);
    REQUIRE(parsed.at("/light").devices.at("kitchen") == "topic/first");
    REQUIRE(errorStream.str().find("duplicate path") != std::string::npos);
}

TEST_CASE("remote_service_mapping_payload_parser_accepts_unknown_nested_fields", "[remote_service]") {
    const std::string payloadText =
        R"({"meta":{"v":1,"arr":[1,-2.5,true,false,null,{"inner":"x"}]},"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"},"extraObject":{"x":1},"extraArray":[1,2,3],"extraBool":true,"extraNull":null,"reason":"switch\nnow"}]})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(parsed.size() == 1U);
    REQUIRE(parsed.contains("/light"));
    REQUIRE(parsed.at("/light").devices.at("kitchen") == "house/kitchen/light/set");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_service_field_without_separator", "[remote_service]") {
    const std::string payloadText =
        R"({"services":[{"path" "/light","devices":{"kitchen":"house/kitchen/light/set"}}]})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage == "service field must contain ':'");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_non_array_services", "[remote_service]") {
    const std::string payloadText =
        R"({"services":{"path":"/light"}})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage == "services must be array");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_invalid_service_devices_shape", "[remote_service]") {
    const std::string payloadText =
        R"({"services":[{"path":"/light","devices":["not-object"]}]})";

    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(payloadText, parsed, errorMessage));
    REQUIRE(errorMessage == "service.devices must be object<string,string>");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_invalid_root_and_service_shapes", "[remote_service]") {
    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload("[]", parsed, errorMessage));
    REQUIRE(errorMessage == "root payload must be object");

    errorMessage.clear();
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(R"({"services":[1]})", parsed, errorMessage));
    REQUIRE(errorMessage == "service entry must be object");

    errorMessage.clear();
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(R"({"services":[]}) trailing)", parsed, errorMessage));
    REQUIRE(errorMessage == "payload contains trailing characters");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_invalid_qos_reason_and_path", "[remote_service]") {
    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic"},"qos":3}]})",
        parsed,
        errorMessage));
    REQUIRE(errorMessage == "service.qos must be integer in range 0..2");

    errorMessage.clear();
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic"},"reason":1}]})",
        parsed,
        errorMessage));
    REQUIRE(errorMessage == "service.reason must be string");

    errorMessage.clear();
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(
        R"({"services":[{"path":"","devices":{"kitchen":"topic"}}]})",
        parsed,
        errorMessage));
    REQUIRE(errorMessage == "service.path must not be empty");
}

TEST_CASE("remote_service_mapping_payload_parser_rejects_invalid_root_fields", "[remote_service]") {
    yaha::RemoteServiceMap parsed{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(R"({})", parsed, errorMessage));
    REQUIRE(errorMessage == "services array is required");

    errorMessage.clear();
    REQUIRE_FALSE(yaha::tryParseRemoteServiceMappingPayload(
        R"({"meta":, "services":[]})",
        parsed,
        errorMessage));
    REQUIRE(errorMessage == "invalid JSON value for root field 'meta'");
}

TEST_CASE("remote_service_monitor_key_path_parser_extracts_key_path", "[remote_service]") {
    const auto keyPath = yaha::tryExtractFileStoreMonitorKeyPath(
        R"({"changeType":"changed","keyPath":"/remoteservice/mapping"})");
    REQUIRE(keyPath.has_value());
    REQUIRE(*keyPath == "/remoteservice/mapping");
}

TEST_CASE("remote_service_monitor_key_path_parser_rejects_invalid_payloads", "[remote_service]") {
    REQUIRE_FALSE(yaha::tryExtractFileStoreMonitorKeyPath("[]").has_value());
    REQUIRE_FALSE(yaha::tryExtractFileStoreMonitorKeyPath(R"({"keyPath":123})").has_value());
    REQUIRE_FALSE(yaha::tryExtractFileStoreMonitorKeyPath(R"({"keyPath" "x"})").has_value());
    REQUIRE_FALSE(yaha::tryExtractFileStoreMonitorKeyPath(R"({"unknown":true})").has_value());
}

TEST_CASE("remote_service_component_get_subscriptions_uses_monitor_prefix_and_qos", "[remote_service]") {
    yaha::RemoteServiceConfig config{};
    config.monitorTopicPrefix = "$MONITOR/CUSTOM";
    config.subscribeQos = yaha::Qos::ExactlyOnce;

    yaha::RemoteServiceComponent component{config};
    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();

    REQUIRE(subscriptions.size() == 1U);
    REQUIRE(subscriptions.contains("$MONITOR/CUSTOM/#"));
    REQUIRE(subscriptions.at("$MONITOR/CUSTOM/#") == yaha::Qos::ExactlyOnce);
}

TEST_CASE("remote_service_component_run_loads_mapping_from_filestore", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.serviceCount() == 1U);
    REQUIRE(component.hasServicePath("/light"));
    REQUIRE(component.mappedTopicFor("/light", "kitchen") == std::optional<std::string>{"house/kitchen/light/set"});

    component.close();
}

TEST_CASE("remote_service_component_run_keeps_empty_map_on_startup_load_failure", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setStatus(kHttpStatusInternalServerError);

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(component.serviceCount() == 0U);
    REQUIRE(fileStore.getCount() >= 1U);

    component.close();
}

TEST_CASE("remote_service_component_run_is_idempotent_after_first_start", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic/old"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();
    const std::uint32_t getCountAfterFirstRun = fileStore.getCount();
    component.run();

    REQUIRE(component.isRunning());
    REQUIRE(fileStore.getCount() == getCountAfterFirstRun);

    component.close();
}

TEST_CASE("remote_service_component_ignores_non_monitor_topic", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic/old"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();
    const std::uint32_t initialGets = fileStore.getCount();

    component.handleMessage(yaha::Message{
        "house/light/set",
        std::string{R"({"keyPath":"/remoteservice/mapping"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(fileStore.getCount() == initialGets);
    REQUIRE(component.hasServicePath("/light"));

    component.close();
}

TEST_CASE("remote_service_component_monitor_matching_key_path_reloads_mapping", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic/old"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    fileStore.setPayload(
        R"({"services":[{"path":"/heating","devices":{"living":"topic/new"}}]})");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"({"keyPath":"/remoteservice/mapping","changeType":"changed"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE_FALSE(component.hasServicePath("/light"));
    REQUIRE(component.hasServicePath("/heating"));
    REQUIRE(component.mappedTopicFor("/heating", "living") == std::optional<std::string>{"topic/new"});

    component.close();
}

TEST_CASE("remote_service_component_monitor_invalid_reload_payload_keeps_previous_map", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"topic/old"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    fileStore.setPayload(R"({"services":[{"path":1}]})");
    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"({"keyPath":"/remoteservice/mapping"})"},
        yaha::Qos::AtLeastOnce,
        false});

    REQUIRE(component.hasServicePath("/light"));
    REQUIRE(component.mappedTopicFor("/light", "kitchen") == std::optional<std::string>{"topic/old"});

    component.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("remote_service_component_resolve_command_builds_outbound_message", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"},"qos":2,"reason":"switch"}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    const yaha::RemoteServiceCommandRequest requestData{
        .path = "/light",
        .deviceId = "kitchen",
        .state = std::string{"on"},
        .token = "token-1"};

    const yaha::RemoteServiceCommandResult result = component.resolveCommand(requestData);
    REQUIRE(result.isSuccess());
    REQUIRE(result.resolvedMessage.has_value());

    const yaha::Message resolved = result.resolvedMessage->clone();
    REQUIRE(resolved.topic() == "house/kitchen/light/set");
    REQUIRE(resolved.qos() == yaha::Qos::ExactlyOnce);
    REQUIRE_FALSE(resolved.retain());
    REQUIRE(std::get<std::string>(resolved.value()) == "on");
    REQUIRE(resolved.reason().size() == 1U);
    REQUIRE(resolved.reason()[0].message == "switch");

    component.close();
}

TEST_CASE("remote_service_component_resolve_command_returns_service_not_found_for_unknown_device", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    const yaha::RemoteServiceCommandRequest requestData{
        .path = "/light",
        .deviceId = "missing-device",
        .state = std::string{"on"},
        .token = "token-2"};

    const yaha::RemoteServiceCommandResult result = component.resolveCommand(requestData);
    REQUIRE(result.status == yaha::RemoteServiceCommandStatus::ServiceNotFound);
    REQUIRE_FALSE(result.resolvedMessage.has_value());

    component.close();
}

TEST_CASE("remote_service_component_publish_command_calls_callback_with_resolved_message", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"},"reason":"switch"}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    std::mutex publishMutex{};
    std::vector<yaha::Message> published{};
    component.setPublishCallback([&publishMutex, &published](const yaha::Message& message) {
        std::lock_guard<std::mutex> lock{publishMutex};
        published.push_back(message.clone());
    });

    const yaha::RemoteServiceCommandRequest requestData{
        .path = "/light",
        .deviceId = "kitchen",
        .state = std::string{"on"},
        .token = "token-3"};

    const yaha::RemoteServiceCommandResult result = component.publishCommand(requestData);
    REQUIRE(result.isSuccess());

    {
        std::lock_guard<std::mutex> lock{publishMutex};
        REQUIRE(published.size() == 1U);
        REQUIRE(published[0].topic() == "house/kitchen/light/set");
        REQUIRE(std::get<std::string>(published[0].value()) == "on");
    }

    component.close();
}

TEST_CASE("remote_service_component_publish_command_returns_publish_failed_when_callback_missing", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    const yaha::RemoteServiceCommandRequest requestData{
        .path = "/light",
        .deviceId = "kitchen",
        .state = std::string{"on"},
        .token = "token-4"};

    const yaha::RemoteServiceCommandResult result = component.publishCommand(requestData);
    REQUIRE(result.status == yaha::RemoteServiceCommandStatus::PublishFailed);
    REQUIRE(result.resolvedMessage.has_value());

    component.close();
}

TEST_CASE("remote_service_component_publish_command_returns_publish_failed_when_callback_throws", "[remote_service]") {
    const std::uint16_t listenPort = reserveFreeLocalPort();
    FileStoreMappingMockServer fileStore{listenPort, "/remoteservice/mapping"};
    fileStore.setPayload(
        R"({"services":[{"path":"/light","devices":{"kitchen":"house/kitchen/light/set"}}]})");

    yaha::RemoteServiceConfig config{};
    config.fileStoreHost = "127.0.0.1";
    config.fileStorePort = listenPort;
    config.mappingKeyPath = "/remoteservice/mapping";

    yaha::RemoteServiceComponent component{config};
    component.run();

    component.setPublishCallback([](const yaha::Message&) {
        throw std::runtime_error{"publish failure"};
    });

    const yaha::RemoteServiceCommandRequest requestData{
        .path = "/light",
        .deviceId = "kitchen",
        .state = std::string{"on"},
        .token = "token-5"};

    const yaha::RemoteServiceCommandResult result = component.publishCommand(requestData);
    REQUIRE(result.status == yaha::RemoteServiceCommandStatus::PublishFailed);
    REQUIRE(result.resolvedMessage.has_value());

    component.close();
}