#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector/receiver_publish_port.h"
#include "yaha/broker_connector/relay_component.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

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
        },
        [](const std::string&) {
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

TEST_CASE("receiver_publish_port_start_publish_and_close", "[broker_connector]") {
    TransportState transportState{};
    transportState.connectResult = true;

    yaha::ReceiverMqttBrokerConfig config{};
    config.enableLifecycleTrace = false;
    config.keepAliveInterval = std::chrono::milliseconds{10};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    REQUIRE(port.start(errorMessage));

    REQUIRE(waitUntil([&port]() {
        return port.isConnected();
    }, std::chrono::milliseconds{500}));

    yaha::Message message{"home/sensor/temp", 21.5, yaha::Qos::AtMostOnce, false};
    yaha::ReceiverPublishOptions options{};
    options.qos = yaha::Qos::ExactlyOnce;
    options.retain = true;

    REQUIRE(port.publish(message, options, errorMessage));
    REQUIRE(transportState.publishCalls.load() == 1);

    {
        std::lock_guard<std::mutex> lock{transportState.publishRecordsMutex};
        REQUIRE(transportState.publishedMessages.size() == 1U);
        REQUIRE(transportState.publishedMessages.front().topic() == "home/sensor/temp");
        REQUIRE(transportState.publishedMessages.front().qos() == yaha::Qos::ExactlyOnce);
        REQUIRE(transportState.publishedMessages.front().retain());
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
    config.reconnectDelay = std::chrono::milliseconds{5};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::ReceiverMqttPublishPort port{config, makeFakeTransport(transportState)};

    std::string errorMessage{};
    REQUIRE(port.start(errorMessage));

    yaha::Message message{"home/sensor/temp", 21.5};
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
    yaha::Message message{"home/sensor/temp", 20.0};
    yaha::ReceiverPublishOptions options{};
    REQUIRE_FALSE(port.publish(message, options, errorMessage));
    REQUIRE(errorMessage == "receiver publish runtime not started");

    port.close();
}

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
    }, std::chrono::milliseconds{500}));

    yaha::Message message{"home/sensor/temp", std::string{"ok"}, yaha::Qos::AtLeastOnce, false};
    message.addReason("updated", "2026-05-01T12:00:00Z");

    yaha::ReceiverPublishOptions options{};
    options.qos = yaha::Qos::AtLeastOnce;
    options.retain = false;
    REQUIRE(port.publish(message, options, errorMessage));

    {
        std::lock_guard<std::mutex> lock{transportState.publishRecordsMutex};
        REQUIRE_FALSE(transportState.publishedMessages.empty());
        REQUIRE(transportState.publishedMessages.back().reason().size() == 1U);
        REQUIRE(transportState.publishedMessages.back().reason().front().message == "updated");
    }

    port.close();
}

TEST_CASE("relay_component_forwards_message_with_mapped_options", "[broker_connector]") {
    FakeReceiverPublishPort receiverPort{};
    std::string startError{};
    REQUIRE(receiverPort.start(startError));

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 3U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};
    config.normalizeQosToAtLeastOnce = true;
    config.retainPassthrough = true;

    yaha::BrokerConnectorComponent component{config};
    component.setReceiverPublishPort(receiverPort);
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::ExactlyOnce;
    sourceMeta.retain = true;
    sourceMeta.dup = true;

    yaha::Message sourceMessage{"home/door/state", std::string{"open"}, yaha::Qos::ExactlyOnce, true};

    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(receiverPort.publishCallCount() == 1);

    const yaha::ReceiverPublishOptions publishOptions = receiverPort.lastPublishOptions();
    REQUIRE(publishOptions.qos == yaha::Qos::AtLeastOnce);
    REQUIRE(publishOptions.retain);
    REQUIRE(publishOptions.dup);

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 1U);
    REQUIRE(counters.failed == 0U);

    component.close();
    receiverPort.close();
}

TEST_CASE("relay_component_retries_then_succeeds", "[broker_connector]") {
    FakeReceiverPublishPort receiverPort{};
    std::string startError{};
    REQUIRE(receiverPort.start(startError));
    receiverPort.setPublishResults({false, false, true});

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 2U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};

    yaha::BrokerConnectorComponent component{config};
    component.setReceiverPublishPort(receiverPort);
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;

    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};
    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(receiverPort.publishCallCount() == 3);

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 1U);
    REQUIRE(counters.failed == 0U);

    component.close();
    receiverPort.close();
}

TEST_CASE("relay_component_counts_failed_after_retry_budget", "[broker_connector]") {
    FakeReceiverPublishPort receiverPort{};
    std::string startError{};
    REQUIRE(receiverPort.start(startError));
    receiverPort.setPublishResults({false, false, false});

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 2U;
    config.publishRetryBackoff = std::chrono::milliseconds{0};

    yaha::BrokerConnectorComponent component{config};
    component.setReceiverPublishPort(receiverPort);
    component.run();

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;

    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};
    REQUIRE_FALSE(component.onIncomingPublish(sourceMessage, sourceMeta));
    REQUIRE(receiverPort.publishCallCount() == 3);

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 1U);
    REQUIRE(counters.forwarded == 0U);
    REQUIRE(counters.failed == 1U);

    component.close();
    receiverPort.close();
}

TEST_CASE("relay_component_rejects_when_not_running", "[broker_connector]") {
    FakeReceiverPublishPort receiverPort{};
    std::string startError{};
    REQUIRE(receiverPort.start(startError));

    yaha::BrokerConnectorComponent component{yaha::RelayPolicyConfig{}};
    component.setReceiverPublishPort(receiverPort);
    REQUIRE_FALSE(component.isRunning());

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::AtLeastOnce;
    yaha::Message sourceMessage{"home/light/state", std::string{"on"}};

    REQUIRE_FALSE(component.onIncomingPublish(sourceMessage, sourceMeta));

    const yaha::RelayCounters counters = component.getStats();
    REQUIRE(counters.received == 0U);
    REQUIRE(counters.forwarded == 0U);
    REQUIRE(counters.failed == 0U);

    receiverPort.close();
}

TEST_CASE("relay_component_supports_passthrough_qos_with_backoff", "[broker_connector]") {
    FakeReceiverPublishPort receiverPort{};
    std::string startError{};
    REQUIRE(receiverPort.start(startError));
    receiverPort.setPublishResults({false, true});

    yaha::RelayPolicyConfig config{};
    config.maxPublishRetries = 1U;
    config.publishRetryBackoff = std::chrono::milliseconds{1};
    config.normalizeQosToAtLeastOnce = false;
    config.retainPassthrough = false;

    yaha::BrokerConnectorComponent component{config};
    component.setReceiverPublishPort(receiverPort);
    component.run();
    REQUIRE(component.isRunning());

    yaha::SourcePublishMeta sourceMeta{};
    sourceMeta.qos = yaha::Qos::ExactlyOnce;
    sourceMeta.retain = true;
    sourceMeta.dup = false;

    yaha::Message sourceMessage{"home/scene", std::string{"movie"}, yaha::Qos::ExactlyOnce, true};
    REQUIRE(component.onIncomingPublish(sourceMessage, sourceMeta));

    const yaha::ReceiverPublishOptions options = receiverPort.lastPublishOptions();
    REQUIRE(options.qos == yaha::Qos::ExactlyOnce);
    REQUIRE_FALSE(options.retain);

    component.close();
    REQUIRE_FALSE(component.isRunning());
    receiverPort.close();
}
