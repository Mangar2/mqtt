#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector/source_http_adapter.h"
#include "yaha/broker_connector/source_lifecycle_manager.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr int k_http_status_ok{200};
constexpr int k_http_status_no_content{204};
constexpr int k_http_status_bad_request{400};
constexpr int k_http_status_service_unavailable{503};
constexpr int k_http_status_internal_server_error{500};
constexpr std::uint32_t k_keep_alive_seconds{15U};
constexpr int k_ready_poll_sleep_ms{5};
constexpr int k_lifecycle_reconnect_ms{20};
constexpr int k_lifecycle_loop_sleep_ms{10};
constexpr int k_lifecycle_keep_alive_ms{30};
constexpr int k_wait_timeout_ms{500};
constexpr int k_wait_sleep_step_ms{10};

class FakeSourceHttpBroker {
public:
    FakeSourceHttpBroker() = default;

    ~FakeSourceHttpBroker() {
        stop();
    }

    void setFailFirstConnect(const bool value) {
        failFirstConnect_ = value;
    }

    void setSubscribeStatusCode(const int statusCode) {
        subscribeStatusCode_ = statusCode;
    }

    void setSubscribeResponseBody(std::string responseBody) {
        subscribeResponseBody_ = std::move(responseBody);
    }

    void setPingPacketHeader(std::string packetHeader) {
        pingPacketHeader_ = std::move(packetHeader);
    }

    void setRequireSendTokenForPing(const bool value) {
        requireSendTokenForPing_ = value;
    }

    void setConnectTokens(std::string sendToken, std::string receiveToken) {
        connectSendToken_ = std::move(sendToken);
        connectReceiveToken_ = std::move(receiveToken);
    }

    void start() {
        if (server_ != nullptr) {
            return;
        }

        server_ = std::make_unique<httplib::Server>();

        server_->Put("/connect", [this](const httplib::Request& request, httplib::Response& response) {
            connectCalls_.fetch_add(1);
            lastConnectBody_ = request.body;

            if (failFirstConnect_ && connectCalls_.load() == 1) {
                response.status = k_http_status_service_unavailable;
                response.set_content("{\"error\":\"unavailable\"}", "application/json");
                return;
            }

            response.status = k_http_status_ok;
            response.set_header("content-type", "application/json; charset=UTF-8");
            response.set_header("packet", "connack");
            response.set_header("version", "1.0");
            response.set_content(
                std::format(
                    "{{\"present\":0,\"token\":{{\"send\":\"{}\",\"receive\":\"{}\"}}}}",
                    connectSendToken_,
                    connectReceiveToken_),
                "application/json");
        });

        server_->Put("/subscribe", [this](const httplib::Request& request, httplib::Response& response) {
            subscribeCalls_.fetch_add(1);
            lastSubscribeBody_ = request.body;
            if (subscribeStatusCode_ != k_http_status_ok) {
                response.status = subscribeStatusCode_;
                response.set_content("{\"error\":\"subscribe_failed\"}", "application/json");
                return;
            }
            response.status = k_http_status_ok;
            response.set_header("content-type", "application/json; charset=UTF-8");
            response.set_header("packet", "suback");
            response.set_header("packetid", request.get_header_value("packetid"));
            if (!subscribeResponseBody_.empty()) {
                response.set_content(subscribeResponseBody_, "application/json");
                return;
            }
            response.set_content(buildDefaultSubscribeResponse(request.body), "application/json");
        });

        server_->Put("/pingreq", [this](const httplib::Request& request, httplib::Response& response) {
            pingCalls_.fetch_add(1);
            lastPingBody_ = request.body;
            if (requireSendTokenForPing_ &&
                request.body.find("\"token\":\"send-token\"") == std::string::npos) {
                response.status = k_http_status_bad_request;
                response.set_content("{\"error\":\"invalid_token\"}", "application/json");
                return;
            }
            response.status = k_http_status_no_content;
            response.set_header("content-type", "application/json; charset=UTF-8");
            response.set_header("packet", pingPacketHeader_);
        });

        server_->Put("/disconnect", [this](const httplib::Request&, httplib::Response& response) {
            disconnectCalls_.fetch_add(1);
            response.status = k_http_status_no_content;
            response.set_header("content-type", "application/json; charset=UTF-8");
        });

        server_->Get("/health", [](const httplib::Request&, httplib::Response& response) {
            response.status = k_http_status_ok;
            response.set_content("ok", "text/plain");
        });

        const int bound = server_->bind_to_any_port("127.0.0.1");
        REQUIRE(bound > 0);
        port_ = static_cast<std::uint16_t>(bound);

        serverThread_ = std::thread([this]() {
            if (server_ != nullptr) {
                server_->listen_after_bind();
            }
        });

        const auto readyDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{k_wait_timeout_ms};
        httplib::Client client{"127.0.0.1", static_cast<int>(port_)};
        while (std::chrono::steady_clock::now() < readyDeadline) {
            const auto health = client.Get("/health");
            if (health && health->status == k_http_status_ok) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{k_ready_poll_sleep_ms});
        }
    }

    void stop() {
        std::unique_ptr<httplib::Server> server{};
        if (server_ != nullptr) {
            server = std::move(server_);
        }

        if (server != nullptr) {
            server->stop();
        }

        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connectCalls() const {
        return connectCalls_.load();
    }

    [[nodiscard]] int subscribeCalls() const {
        return subscribeCalls_.load();
    }

    [[nodiscard]] int pingCalls() const {
        return pingCalls_.load();
    }

    [[nodiscard]] int disconnectCalls() const {
        return disconnectCalls_.load();
    }

    [[nodiscard]] std::string lastConnectBody() const {
        return lastConnectBody_;
    }

    [[nodiscard]] std::string lastSubscribeBody() const {
        return lastSubscribeBody_;
    }

    static httplib::Result sendPublishTo(const std::uint16_t listenerPort,
                                         const std::string& qos,
                                         const std::string& packetId,
                                         const std::string& payload,
                                         const std::string& retain = "0",
                                         const std::string& dup = "0") {
        httplib::Client client{"127.0.0.1", static_cast<int>(listenerPort)};
        httplib::Headers headers{
            {"content-type", "application/json; charset=UTF-8"},
            {"version", "1.0"},
            {"qos", qos},
            {"retain", retain},
            {"dup", dup},
            {"packetid", packetId}
        };
        return client.Put("/publish", headers, payload, "application/json");
    }

    static httplib::Result sendPubrelTo(const std::uint16_t listenerPort,
                                        const std::string& packetId) {
        httplib::Client client{"127.0.0.1", static_cast<int>(listenerPort)};
        httplib::Headers headers{
            {"content-type", "text/plain; charset=UTF-8"},
            {"version", "1.0"},
            {"packetid", packetId}
        };
        return client.Put("/pubrel", headers, "{\"token\":\"recv-token\"}", "application/json");
    }

private:
    [[nodiscard]] static std::string buildDefaultSubscribeResponse(const std::string& subscribeBody) {
        const std::string topicsKey{"\"topics\":{"};
        const std::size_t topicsPos = subscribeBody.find(topicsKey);
        if (topicsPos == std::string::npos) {
            return "{\"qos\":[]}";
        }

        const std::size_t objectStart = topicsPos + topicsKey.size();
        std::size_t objectEnd = objectStart;
        int depth = 1;
        while (objectEnd < subscribeBody.size() && depth > 0) {
            if (subscribeBody[objectEnd] == '{') {
                ++depth;
            } else if (subscribeBody[objectEnd] == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            ++objectEnd;
        }

        const std::string topicsObject = subscribeBody.substr(
            objectStart,
            objectEnd > objectStart ? objectEnd - objectStart : 0U);

        std::size_t topicCount = 0U;
        bool inString = false;
        for (const char chr : topicsObject) {
            if (chr == '"') {
                inString = !inString;
                continue;
            }
            if (chr == ':' && !inString) {
                topicCount += 1U;
            }
        }

        std::ostringstream stream{};
        stream << "{\"qos\":[";
        for (std::size_t idx = 0U; idx < topicCount; ++idx) {
            if (idx != 0U) {
                stream << ',';
            }
            stream << 1;
        }
        stream << "]}";
        return stream.str();
    }

    std::unique_ptr<httplib::Server> server_{};
    std::thread serverThread_{};
    std::uint16_t port_{0U};

    bool failFirstConnect_{false};
    int subscribeStatusCode_{k_http_status_ok};
    std::string subscribeResponseBody_{};
    bool requireSendTokenForPing_{false};
    std::string pingPacketHeader_{"pingresp"};
    std::string connectSendToken_{"send-token"};
    std::string connectReceiveToken_{"recv-token"};
    std::atomic<int> connectCalls_{0};
    std::atomic<int> subscribeCalls_{0};
    std::atomic<int> pingCalls_{0};
    std::atomic<int> disconnectCalls_{0};

    std::string lastConnectBody_{};
    std::string lastSubscribeBody_{};
    std::string lastPingBody_{};
};

bool waitUntil(const std::function<bool()>& condition,
               const std::chrono::milliseconds timeout,
               const std::chrono::milliseconds sleepStep =
                   std::chrono::milliseconds{k_wait_sleep_step_ms}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(sleepStep);
    }
    return condition();
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("source_adapter_connect_subscribe_and_callback_publish", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    {
        httplib::Client healthClient{"127.0.0.1", static_cast<int>(sourceBroker.port())};
        const auto health = healthClient.Get("/health");
        REQUIRE(health != nullptr);
        REQUIRE(health->status == k_http_status_ok);
    }

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.clean = true;
    config.keepAliveSeconds = k_keep_alive_seconds;
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};

    std::mutex callbackMutex{};
    std::vector<yaha::Message> callbackMessages{};
    std::vector<yaha::SourcePublishMeta> callbackMeta{};
    adapter.setIncomingPublishCallback([&callbackMutex, &callbackMessages, &callbackMeta](
                                          const yaha::Message& message,
                                          const yaha::SourcePublishMeta& meta) {
        std::lock_guard<std::mutex> lock{callbackMutex};
        callbackMessages.push_back(message);
        callbackMeta.push_back(meta);
    });

    std::string errorMessage{};
    if (!adapter.connectAndSubscribe(errorMessage)) {
        FAIL("connectAndSubscribe failed: " + errorMessage
             + " sourcePort=" + std::to_string(sourceBroker.port())
             + " connectCalls=" + std::to_string(sourceBroker.connectCalls()));
    }
    REQUIRE(adapter.isConnected());
    REQUIRE(sourceBroker.connectCalls() >= 1);
    REQUIRE(sourceBroker.subscribeCalls() >= 1);
    REQUIRE_FALSE(adapter.listenerPort() == 0U);
    REQUIRE(sourceBroker.lastConnectBody().find("\"keepAlive\":15000") != std::string::npos);

    const auto publishResponse = FakeSourceHttpBroker::sendPublishTo(
        adapter.listenerPort(),
        "1",
        "7",
        "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/kitchen/temp\",\"value\":21.5,\"reason\":[{\"message\":\"sensor update\",\"timestamp\":\"2026-05-08T10:00:00Z\"}]}}"
    );

    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_http_status_no_content);
    REQUIRE(publishResponse->get_header_value("packet") == "puback");

    REQUIRE(waitUntil([&callbackMutex, &callbackMessages]() {
        std::lock_guard<std::mutex> lock{callbackMutex};
        return !callbackMessages.empty();
    }, std::chrono::milliseconds{k_wait_timeout_ms}));

    {
        std::lock_guard<std::mutex> lock{callbackMutex};
        REQUIRE(callbackMessages.size() == 1U);
        REQUIRE(callbackMessages.front().topic() == "home/kitchen/temp");
        REQUIRE(std::holds_alternative<double>(callbackMessages.front().value()));
        REQUIRE(callbackMessages.front().reason().size() == 1U);
        REQUIRE(callbackMessages.front().reason().front().message == "sensor update");
        REQUIRE(callbackMessages.front().reason().front().timestamp == "2026-05-08T10:00:00Z");
        REQUIRE(callbackMessages.front().rawPayload().has_value());
        REQUIRE(*callbackMessages.front().rawPayload() ==
            "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/kitchen/temp\",\"value\":21.5,\"reason\":[{\"message\":\"sensor update\",\"timestamp\":\"2026-05-08T10:00:00Z\"}]}}");
        REQUIRE(callbackMeta.size() == 1U);
        REQUIRE(callbackMeta.front().qos == yaha::Qos::AtLeastOnce);
        REQUIRE(callbackMeta.front().packetId.has_value());
        REQUIRE(*callbackMeta.front().packetId == 7U);
    }

    adapter.close();
    REQUIRE(sourceBroker.disconnectCalls() == 1);
    sourceBroker.stop();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("source_adapter_qos2_publish_and_pubrel_ack_sequence", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    const auto publishResponse = FakeSourceHttpBroker::sendPublishTo(
        adapter.listenerPort(),
        "2",
        "42",
        "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/door/state\",\"value\":\"open\"}}"
    );

    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_http_status_no_content);
    REQUIRE(publishResponse->get_header_value("packet") == "pubrec");
    REQUIRE(publishResponse->get_header_value("packetid") == "42");

    const auto pubrelResponse = FakeSourceHttpBroker::sendPubrelTo(adapter.listenerPort(), "42");
    REQUIRE(pubrelResponse != nullptr);
    REQUIRE(pubrelResponse->status == k_http_status_no_content);
    REQUIRE(pubrelResponse->get_header_value("packet") == "pubcomp");
    REQUIRE(pubrelResponse->get_header_value("packetid") == "42");

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_lifecycle_retries_connect_and_replays_subscribe", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setFailFirstConnect(true);
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig sourceConfig{};
    sourceConfig.brokerHost = "127.0.0.1";
    sourceConfig.brokerPort = sourceBroker.port();
    sourceConfig.clientId = "connector-test-client";
    sourceConfig.listenerHost = "127.0.0.1";
    sourceConfig.listenerPort = 0U;
    sourceConfig.keepAliveSeconds = 1U;
    sourceConfig.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{sourceConfig};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    yaha::SourceLifecycleConfig lifecycleConfig{};
    lifecycleConfig.reconnectDelay = std::chrono::milliseconds{k_lifecycle_reconnect_ms};
    lifecycleConfig.loopSleep = std::chrono::milliseconds{k_lifecycle_loop_sleep_ms};
    lifecycleConfig.keepAliveInterval = std::chrono::milliseconds{k_lifecycle_keep_alive_ms};
    lifecycleConfig.enableTrace = false;

    yaha::SourceLifecycleManager manager{adapter, lifecycleConfig};
    manager.run();

    REQUIRE(waitUntil([&sourceBroker]() {
        return sourceBroker.connectCalls() >= 2;
    }, std::chrono::milliseconds{1000}));

    REQUIRE(waitUntil([&sourceBroker]() {
        return sourceBroker.subscribeCalls() >= 1;
    }, std::chrono::milliseconds{1000}));

    REQUIRE(waitUntil([&sourceBroker]() {
        return sourceBroker.pingCalls() >= 1;
    }, std::chrono::milliseconds{1000}));

    manager.close();
    REQUIRE_FALSE(manager.isRunning());

    sourceBroker.stop();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("source_adapter_qos0_publish_with_dup_retain_flags", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}, {"sensor/#", yaha::Qos::AtMostOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};

    std::mutex callbackMutex{};
    std::vector<yaha::SourcePublishMeta> metaValues{};
    adapter.setIncomingPublishCallback([&callbackMutex, &metaValues](const yaha::Message&,
                                                                      const yaha::SourcePublishMeta& meta) {
        std::lock_guard<std::mutex> lock{callbackMutex};
        metaValues.push_back(meta);
    });

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    const auto publishResponse = FakeSourceHttpBroker::sendPublishTo(
        adapter.listenerPort(),
        "0",
        "abc",
        "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/lamp\",\"value\":\"on\"}}",
        "1",
        "1");

    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_http_status_no_content);
    REQUIRE(publishResponse->get_header_value("packet").empty());

    REQUIRE(waitUntil([&callbackMutex, &metaValues]() {
        std::lock_guard<std::mutex> lock{callbackMutex};
        return !metaValues.empty();
    }, std::chrono::milliseconds{k_wait_timeout_ms}));

    {
        std::lock_guard<std::mutex> lock{callbackMutex};
        REQUIRE(metaValues.size() == 1U);
        REQUIRE(metaValues.front().qos == yaha::Qos::AtMostOnce);
        REQUIRE(metaValues.front().retain);
        REQUIRE(metaValues.front().dup);
        REQUIRE_FALSE(metaValues.front().packetId.has_value());
    }

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_invalid_publish_payload_returns_400", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};

    std::atomic<int> callbackCount{0};
    adapter.setIncomingPublishCallback([&callbackCount](const yaha::Message&, const yaha::SourcePublishMeta&) {
        callbackCount.fetch_add(1);
    });

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    const auto publishResponse = FakeSourceHttpBroker::sendPublishTo(
        adapter.listenerPort(),
        "1",
        "9",
        "{\"token\":\"send-token\",\"message\":{\"value\":21.5}}"
    );

    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_http_status_bad_request);
    REQUIRE(callbackCount.load() == 0);

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_publish_invalid_bool_headers_fallback_to_false", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};

    std::mutex callbackMutex{};
    std::optional<yaha::SourcePublishMeta> lastMeta{};
    adapter.setIncomingPublishCallback([&callbackMutex, &lastMeta](const yaha::Message&, const yaha::SourcePublishMeta& meta) {
        std::lock_guard<std::mutex> lock{callbackMutex};
        lastMeta = meta;
    });

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    const auto publishResponse = FakeSourceHttpBroker::sendPublishTo(
        adapter.listenerPort(),
        "0",
        "",
        "{\"message\":{\"topic\":\"home/state\",\"value\":\"ok\"}}",
        "  maybe  ",
        "  ???  ");

    REQUIRE(publishResponse != nullptr);
    REQUIRE(publishResponse->status == k_http_status_no_content);

    {
        std::lock_guard<std::mutex> lock{callbackMutex};
        REQUIRE(lastMeta.has_value());
        REQUIRE_FALSE(lastMeta->retain);
        REQUIRE_FALSE(lastMeta->dup);
    }

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_ping_when_disconnected_returns_false", "[broker_connector]") {
    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = 1U;
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};

    std::string errorMessage{};
    REQUIRE_FALSE(adapter.ping(errorMessage));
    REQUIRE(errorMessage == "source adapter not connected");
    REQUIRE_FALSE(adapter.isConnected());
}

TEST_CASE("source_adapter_connect_fails_when_broker_unreachable", "[broker_connector]") {
    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = 1U;
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE_FALSE(adapter.connectAndSubscribe(errorMessage));
    REQUIRE(errorMessage == "source connect request failed");
    REQUIRE_FALSE(adapter.isConnected());
}

TEST_CASE("source_adapter_subscribe_status_error_propagates", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setSubscribeStatusCode(k_http_status_internal_server_error);
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE_FALSE(adapter.connectAndSubscribe(errorMessage));
    REQUIRE(errorMessage ==
            ("source subscribe failed with status " +
             std::to_string(k_http_status_internal_server_error)));
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_subscribe_rejected_qos_fails_connect", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setSubscribeResponseBody("{\"qos\":[128]}");
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE_FALSE(adapter.connectAndSubscribe(errorMessage));
    REQUIRE(errorMessage.find("source subscribe rejected topic index") != std::string::npos);
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_ping_wrong_packet_sets_disconnected", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setPingPacketHeader("broken");
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    errorMessage.clear();
    REQUIRE_FALSE(adapter.ping(errorMessage));
    REQUIRE(errorMessage.starts_with("ping response missing packet=pingresp"));
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_ping_uses_send_token", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setRequireSendTokenForPing(true);
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    errorMessage.clear();
    REQUIRE(adapter.ping(errorMessage));
    REQUIRE(errorMessage.empty());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_ping_transport_failure_sets_disconnected", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    sourceBroker.stop();

    errorMessage.clear();
    REQUIRE_FALSE(adapter.ping(errorMessage));
    REQUIRE(errorMessage.find("ping request failed using send token") != std::string::npos);
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
}

TEST_CASE("source_adapter_ping_failure_without_fallback_sets_disconnected", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setConnectTokens("shared-token", "shared-token");
    sourceBroker.setPingPacketHeader("broken");
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    errorMessage.clear();
    REQUIRE_FALSE(adapter.ping(errorMessage));
    REQUIRE(errorMessage.find("using send token") != std::string::npos);
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_ping_send_failure_does_not_try_receive_token", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.setRequireSendTokenForPing(true);
    sourceBroker.setConnectTokens("bad-send-token", "recv-token");
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));

    errorMessage.clear();
    REQUIRE_FALSE(adapter.ping(errorMessage));
    REQUIRE(errorMessage.find("using send token") != std::string::npos);
    REQUIRE(errorMessage.find("fallback") == std::string::npos);
    REQUIRE(sourceBroker.pingCalls() == 1);
    REQUIRE_FALSE(adapter.isConnected());

    adapter.close();
    sourceBroker.stop();
}

TEST_CASE("source_adapter_binds_listener_host_separately_from_advertised_host", "[broker_connector]") {
    FakeSourceHttpBroker sourceBroker{};
    sourceBroker.start();

    yaha::SourceHttpBrokerConfig config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = sourceBroker.port();
    config.clientId = "connector-test-client";
    config.listenerHost = "yaha2";
    config.listenerBindHost = "127.0.0.1";
    config.listenerPort = 0U;
    config.subscribeTopics = {{"home/#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{config};
    adapter.setIncomingPublishCallback([](const yaha::Message&, const yaha::SourcePublishMeta&) {});

    std::string errorMessage{};
    REQUIRE(adapter.connectAndSubscribe(errorMessage));
    REQUIRE(sourceBroker.lastConnectBody().find("\"host\":\"yaha2\"") != std::string::npos);

    adapter.close();
    sourceBroker.stop();
}
