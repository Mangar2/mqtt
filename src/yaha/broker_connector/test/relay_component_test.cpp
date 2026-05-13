#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector/receiver_publish_port.h"
#include "yaha/broker_connector/relay_component.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr int k_wait_timeout_ms{500};
constexpr int k_keep_alive_ms{10};
constexpr int k_loop_sleep_ms{5};
constexpr double k_temperature_21_5{21.5};
constexpr double k_temperature_20_0{20.0};
constexpr int k_non_std_exception_value{7};

bool waitUntil(const std::function<bool()>& condition,
               const std::chrono::milliseconds timeout,
               const std::chrono::milliseconds sleepStep = std::chrono::milliseconds{10}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(sleepStep);
    }
    return condition();
}

struct TransportState {
    std::atomic<bool> connected{false};
    std::atomic<int> connectCalls{0};
    std::atomic<int> disconnectCalls{0};
    std::atomic<int> publishCalls{0};
    std::atomic<int> pingCalls{0};
    bool connectResult{true};

    mutable std::mutex publishRecordsMutex{};
    std::vector<yaha::Message> publishedMessages{};
};

yaha::YahaMqttClient::Transport makeFakeTransport(TransportState& state) {
    return yaha::YahaMqttClient::Transport{
        [&state](const yaha::YahaMqttClient::Config&) {
            state.connectCalls.fetch_add(1);
            state.connected.store(state.connectResult);
            return state.connectResult;
        },
        [&state]() {
            state.disconnectCalls.fetch_add(1);
            state.connected.store(false);
        },
        [&state](const yaha::Message& message) {
            state.publishCalls.fetch_add(1);
            std::lock_guard<std::mutex> lock{state.publishRecordsMutex};
            state.publishedMessages.push_back(message);
        },
        [](const std::string&, const yaha::Qos) {
            return true;
        },
        [](const std::string&) {
            return true;
        },
        []() -> std::optional<yaha::Message> {
            return std::nullopt;
        },
        [&state]() {
            state.pingCalls.fetch_add(1);
        },
        [&state]() {
            return state.connected.load();
        }
    };
}

class FakeReceiverPublishPort final : public yaha::ReceiverPublishPort {
public:
    void setPublishResults(std::vector<bool> results) {
        publishResults_ = std::move(results);
    }

    [[nodiscard]] bool start(std::string& errorMessage) override {
        (void)errorMessage;
        started_ = true;
        return true;
    }

    void close() override {
        started_ = false;
    }

    [[nodiscard]] bool publish(const yaha::Message& message,
                               const yaha::ReceiverPublishOptions& options,
                               std::string& errorMessage) override {
        lastPublishedMessage_ = message;
        lastPublishOptions_ = options;
        publishCallCount_ += 1;

        if (!started_) {
            errorMessage = "not started";
            return false;
        }

        if (publishResults_.empty()) {
            return true;
        }

        const bool result = publishResults_.front();
        publishResults_.erase(publishResults_.begin());
        if (!result) {
            errorMessage = "publish failed";
        }
        return result;
    }

    [[nodiscard]] bool isConnected() const override {
        return started_;
    }

    [[nodiscard]] int publishCallCount() const {
        return publishCallCount_;
    }

    [[nodiscard]] const yaha::Message& lastPublishedMessage() const {
        return lastPublishedMessage_;
    }

    [[nodiscard]] yaha::ReceiverPublishOptions lastPublishOptions() const {
        return lastPublishOptions_;
    }

private:
    bool started_{false};
    int publishCallCount_{0};
    yaha::Message lastPublishedMessage_{"", std::string{}};
    yaha::ReceiverPublishOptions lastPublishOptions_{};
    std::vector<bool> publishResults_{};
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("receiver_publish_port_start_publish_and_close", "[broker_connector]") {
    TransportState transportState{};
    transportState.connectResult = true;

    yaha::ReceiverMqttBrokerConfig config{};
    config.enableLifecycleTrace = false;
    config.keepAliveInterval = std::chrono::milliseconds{k_keep_alive_ms};
    config.loopSleep = std::chrono::milliseconds{k_loop_sleep_ms};

    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    REQUIRE(port.start(errorMessage));

    REQUIRE(waitUntil([&port]() {
        return port.isConnected();
    }, std::chrono::milliseconds{k_wait_timeout_ms}));

    yaha::Message message{"home/sensor/temp", k_temperature_21_5, yaha::Qos::AtMostOnce, false};
    yaha::ReceiverPublishOptions options{};
    options.qos = yaha::Qos::ExactlyOnce;
    options.retain = true;
    options.dup = true;

    REQUIRE(port.publish(message, options, errorMessage));
    REQUIRE(transportState.publishCalls.load() == 1);

    {
        std::lock_guard<std::mutex> lock{transportState.publishRecordsMutex};
        REQUIRE(transportState.publishedMessages.size() == 1U);
        REQUIRE(transportState.publishedMessages.front().topic() == "home/sensor/temp");
        REQUIRE(transportState.publishedMessages.front().qos() == yaha::Qos::ExactlyOnce);
        REQUIRE(transportState.publishedMessages.front().retain());
        REQUIRE(transportState.publishedMessages.front().dup());
    }

    port.close();
    REQUIRE_FALSE(port.isConnected());
    REQUIRE(transportState.disconnectCalls.load() >= 1);
}

TEST_CASE("receiver_publish_port_disconnected_publish_returns_false", "[broker_connector]") {
    TransportState transportState{};
    transportState.connectResult = false;

    yaha::ReceiverMqttBrokerConfig config{};
    config.enableLifecycleTrace = false;
    config.reconnectDelay = std::chrono::milliseconds{k_loop_sleep_ms};
    config.loopSleep = std::chrono::milliseconds{k_loop_sleep_ms};

    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    REQUIRE(port.start(errorMessage));

    yaha::Message message{"home/sensor/temp", k_temperature_21_5};
    yaha::ReceiverPublishOptions options{};
    REQUIRE_FALSE(port.publish(message, options, errorMessage));
    REQUIRE(errorMessage.find("receiver publish failed") != std::string::npos);

    port.close();
}

TEST_CASE("receiver_publish_port_publish_before_start_returns_false", "[broker_connector]") {
    TransportState transportState{};
    transportState.connectResult = true;

    yaha::ReceiverMqttBrokerConfig config{};
    config.enableLifecycleTrace = false;

    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    yaha::Message message{"home/sensor/temp", k_temperature_20_0};
    yaha::ReceiverPublishOptions options{};
    REQUIRE_FALSE(port.publish(message, options, errorMessage));
    REQUIRE(errorMessage == "receiver publish runtime not started");

    port.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("receiver_publish_port_start_is_idempotent_and_preserves_reason", "[broker_connector]") {
    TransportState transportState{};
    transportState.connectResult = true;

    yaha::ReceiverMqttBrokerConfig config{};
    config.enableLifecycleTrace = false;
    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    REQUIRE(port.start(errorMessage));
    REQUIRE(port.start(errorMessage));

    REQUIRE(waitUntil([&port]() {
        return port.isConnected();
    }, std::chrono::milliseconds{k_wait_timeout_ms}));

    yaha::Message message{"home/sensor/temp", std::string{"ok"}, yaha::Qos::AtLeastOnce, false};
    message.addReason("updated", "2026-05-01T12:00:00Z");
    message.setRawPayload("{\"token\":\"send-token\",\"message\":{\"topic\":\"home/sensor/temp\",\"value\":\"ok\",\"reason\":[{\"message\":\"updated\",\"timestamp\":\"2026-05-01T12:00:00Z\"}]}}");

    yaha::ReceiverPublishOptions options{};
    options.qos = yaha::Qos::AtLeastOnce;
    options.retain = false;
    REQUIRE(port.publish(message, options, errorMessage));

    {
        std::lock_guard<std::mutex> lock{transportState.publishRecordsMutex};
        REQUIRE_FALSE(transportState.publishedMessages.empty());
        REQUIRE(transportState.publishedMessages.back().reason().size() == 1U);
        REQUIRE(transportState.publishedMessages.back().reason().front().message == "updated");
        REQUIRE(transportState.publishedMessages.back().rawPayload().has_value());
        REQUIRE(*transportState.publishedMessages.back().rawPayload() ==
            "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/sensor/temp\",\"value\":\"ok\",\"reason\":[{\"message\":\"updated\",\"timestamp\":\"2026-05-01T12:00:00Z\"}]}}");
    }

    port.close();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("relay_component_forwards_message_with_mapped_options", "[broker_connector]") {
    struct PublishSink {
        std::vector<yaha::Message> messages{};
        std::vector<bool> outcomes{};
        int callCount{0};

        void publish(const yaha::Message& message) {
            callCount += 1;
            messages.push_back(message);
            if (!outcomes.empty()) {
                const bool outcome = outcomes.front();
                outcomes.erase(outcomes.begin());
                if (!outcome) {
                    throw std::runtime_error{"publish failed"};
                }
            }
        }
    } sink;

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 3U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};
    config.normalizeQosToAtLeastOnce = true;
    config.retainPassthrough = true;

    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&sink](const yaha::Message& message) {
        sink.publish(message);
    });
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::ExactlyOnce;
    sourceMeta.retain = true;
    sourceMeta.dup = true;

    yaha::Message sourceMessage{"home/door/state", std::string{"open"}, yaha::Qos::ExactlyOnce, true};
    sourceMessage.addReason("src-reason", "2026-05-01T12:00:00Z");
    sourceMessage.setRawPayload("{\"token\":\"send-token\",\"message\":{\"topic\":\"home/door/state\",\"value\":\"open\",\"reason\":[{\"message\":\"src-reason\",\"timestamp\":\"2026-05-01T12:00:00Z\"}]}}");

    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(sink.callCount == 1);
    REQUIRE(sink.messages.size() == 1U);
    REQUIRE(sink.messages.front().qos() == yaha::Qos::AtLeastOnce);
    REQUIRE(sink.messages.front().retain());
    REQUIRE(sink.messages.front().dup());
    REQUIRE(sink.messages.front().reason().size() == 1U);
    REQUIRE(sink.messages.front().reason().front().message == "src-reason");
    REQUIRE(sink.messages.front().rawPayload().has_value());
    REQUIRE(*sink.messages.front().rawPayload() ==
        "{\"token\":\"send-token\",\"message\":{\"topic\":\"home/door/state\",\"value\":\"open\",\"reason\":[{\"message\":\"src-reason\",\"timestamp\":\"2026-05-01T12:00:00Z\"}]}}");

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 1U);
    REQUIRE(counters.failed == 0U);

    component.close();
}

TEST_CASE("relay_component_retries_then_succeeds", "[broker_connector]") {
    struct PublishSink {
        std::vector<bool> outcomes{};
        int callCount{0};

        void publish(const yaha::Message& message) {
            (void)message;
            callCount += 1;
            const bool outcome = outcomes.front();
            outcomes.erase(outcomes.begin());
            if (!outcome) {
                throw std::runtime_error{"publish failed"};
            }
        }
    } sink;
    sink.outcomes = {false, false, true};

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 2U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};

    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&sink](const yaha::Message& message) {
        sink.publish(message);
    });
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;

    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};
    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(sink.callCount == 3);

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 1U);
    REQUIRE(counters.failed == 0U);

    component.close();
}

TEST_CASE("relay_component_counts_failed_after_retry_budget", "[broker_connector]") {
    struct PublishSink {
        int callCount{0};

        void publish(const yaha::Message& message) {
            (void)message;
            callCount += 1;
            throw std::runtime_error{"publish failed"};
        }
    } sink;

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 2U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};

    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&sink](const yaha::Message& message) {
        sink.publish(message);
    });
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;

    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};
    REQUIRE_FALSE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(sink.callCount == 3);

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 0U);
    REQUIRE(counters.failed == 1U);

    component.close();
}

TEST_CASE("relay_component_rejects_when_not_running", "[broker_connector]") {
    yaha::BrokerConnectorComponent component{yaha::RelayPolicyConfig{}};
    component.setPublishCallback([](const yaha::Message&) {
    });
    REQUIRE_FALSE(component.isRunning());

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;
    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};

    REQUIRE_FALSE(component.onIncomingPublish(sourceMessage, sourceMeta));

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 0U);
    REQUIRE(counters.forwarded == 0U);
    REQUIRE(counters.failed == 0U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("relay_component_supports_passthrough_qos_with_backoff", "[broker_connector]") {
    struct PublishSink {
        std::vector<yaha::Message> messages{};
        bool firstCall{true};

        void publish(const yaha::Message& message) {
            messages.push_back(message);
            if (firstCall) {
                firstCall = false;
                throw std::runtime_error{"publish failed"};
            }
        }
    } sink;

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 1U;
    config.publishRetryBackoff = std::chrono::milliseconds{1};
    config.normalizeQosToAtLeastOnce = false;
    config.retainPassthrough = false;

    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&sink](const yaha::Message& message) {
        sink.publish(message);
    });
    component.run();
    REQUIRE(component.isRunning());

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::ExactlyOnce;
    sourceMeta.retain = true;
    sourceMeta.dup = true;

    yaha::Message sourceMessage{"home/scene", std::string{"movie"}, yaha::Qos::ExactlyOnce, true};
    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(sink.messages.size() == 2U);
    REQUIRE(sink.messages.back().qos() == yaha::Qos::ExactlyOnce);
    REQUIRE_FALSE(sink.messages.back().retain());
    REQUIRE(sink.messages.back().dup());

    component.close();
    REQUIRE_FALSE(component.isRunning());
}

TEST_CASE("relay_component_with_source_adapter_covers_lifecycle_and_reason_copy", "[broker_connector]") {
    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 0U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};
    config.normalizeQosToAtLeastOnce = false;
    config.retainPassthrough = true;

    yaha::BrokerConnectorComponent component{config};

    const auto subscriptions = component.getSubscriptions();
    REQUIRE(subscriptions.empty());
    component.handleMessage(yaha::Message{"ignored/topic", std::string{"value"}});

    component.setPublishCallback([](const yaha::Message&) {
        throw k_non_std_exception_value;
    });

    component.run();
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::ExactlyOnce;
    sourceMeta.retain = true;

    yaha::Message sourceMessage{"home/reason", std::string{"x"}, yaha::Qos::AtMostOnce, false};
    sourceMessage.addReason("r1", "2026-05-01T00:00:00Z");
    REQUIRE_FALSE(component.onIncomingPublish(sourceMessage, sourceMeta));

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 0U);
    REQUIRE(counters.failed == 1U);

    component.close();
    REQUIRE_FALSE(component.isRunning());
}

TEST_CASE("relay_component_imqtt_component_surface_and_source_lifecycle_paths", "[broker_connector]") {
    yaha::SourceHttpBrokerConfig sourceConfig{};
    sourceConfig.brokerHost = "127.0.0.1";
    sourceConfig.brokerPort = 1U;
    sourceConfig.listenerHost = "127.0.0.1";
    sourceConfig.listenerPort = 0U;
    sourceConfig.subscribeTopics = {{"#", yaha::Qos::AtLeastOnce}};

    yaha::SourceHttpBrokerAdapter adapter{sourceConfig};
    yaha::SourceLifecycleConfig lifecycleConfig{};
    lifecycleConfig.reconnectDelay = std::chrono::milliseconds{1};
    lifecycleConfig.loopSleep = std::chrono::milliseconds{1};
    lifecycleConfig.keepAliveInterval = std::chrono::milliseconds{2};
    lifecycleConfig.enableTrace = false;

    yaha::RelayPolicyConfig config{};
    yaha::BrokerConnectorComponent component{config};

    REQUIRE(component.getSubscriptions().empty());
    component.handleMessage(yaha::Message{"receiver/topic", std::string{"ignored"}});

    component.setSourceAdapter(adapter, lifecycleConfig);
    component.setPublishCallback([](const yaha::Message&) {
    });

    component.close();
}

TEST_CASE("relay_component_retries_on_non_std_exception", "[broker_connector]") {
    int callCount = 0;

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 1U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};

    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&callCount](const yaha::Message&) {
        callCount += 1;
        if (callCount == 1) {
            throw k_non_std_exception_value;
        }
    });
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;
    yaha::Message sourceMessage{"home/retry/nonstd", std::string{"ok"}};

    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(callCount == 2);

    component.close();
}

TEST_CASE("relay_component_clears_dup_for_qos0_output", "[broker_connector]") {
    yaha::RelayPolicyConfig config{};
    config.normalizeQosToAtLeastOnce = true;
    config.retainPassthrough = true;
    config.maxPublishRetries = 0U;

    std::vector<yaha::Message> published{};
    yaha::BrokerConnectorComponent component{config};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
    });
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtMostOnce;
    sourceMeta.retain = false;
    sourceMeta.dup = true;

    yaha::Message sourceMessage{"home/qos0", std::string{"v"}, yaha::Qos::AtMostOnce, false};
    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(published.size() == 1U);
    REQUIRE(published.front().qos() == yaha::Qos::AtMostOnce);
    REQUIRE_FALSE(published.front().dup());

    component.close();
}
