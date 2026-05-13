#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

// NOLINTBEGIN(readability-magic-numbers)
namespace {

class RecordingComponent final : public yaha::IMqttComponent {
public:
    explicit RecordingComponent(yaha::SubscriptionMap subscriptions)
        : subscriptions_(std::move(subscriptions)) {}

    [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
        return subscriptions_;
    }

    void handleMessage(const yaha::Message& message) override {
        last_callback_set_during_handle_ = callback_set_;
        handled_messages_.push_back(message);
    }

    void setPublishCallback(yaha::PublishCallback callback) override {
        callback_set_ = true;
        publish_callback_ = std::move(callback);
    }

    void run() override {}

    void close() override {}

    void publishFromComponent(const yaha::Message& message) const {
        if (publish_callback_) {
            publish_callback_(message);
        }
    }

    [[nodiscard]] yaha::PublishResult publishFromComponentWithResult(
        const yaha::Message& message) const {
        if (!publish_callback_) {
            return yaha::PublishResult::fail(yaha::PublishFailureCategory::CallbackMissing,
                                             "callback_missing");
        }

        return publish_callback_(message);
    }

    [[nodiscard]] bool callbackSet() const noexcept {
        return callback_set_;
    }

    [[nodiscard]] bool callbackSetDuringHandle() const noexcept {
        return last_callback_set_during_handle_;
    }

    [[nodiscard]] std::size_t handledCount() const noexcept {
        return handled_messages_.size();
    }

private:
    yaha::SubscriptionMap subscriptions_;
    yaha::PublishCallback publish_callback_{};
    bool callback_set_{false};
    bool last_callback_set_during_handle_{false};
    std::vector<yaha::Message> handled_messages_{};
};

class MutatingSubscriptionsComponent final : public yaha::IMqttComponent {
public:
    MutatingSubscriptionsComponent(yaha::SubscriptionMap initialSubscriptions,
                                   yaha::SubscriptionMap updatedSubscriptions)
        : subscriptions_(std::move(initialSubscriptions))
        , updatedSubscriptions_(std::move(updatedSubscriptions)) {}

    [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
        return subscriptions_;
    }

    void handleMessage(const yaha::Message& message) override {
        (void)message;
        subscriptions_ = updatedSubscriptions_;
        handled_messages_ += 1U;
    }

    void setPublishCallback(yaha::PublishCallback callback) override {
        publish_callback_ = std::move(callback);
    }

    void run() override {}

    void close() override {}

    [[nodiscard]] std::size_t handledCount() const noexcept {
        return handled_messages_;
    }

private:
    mutable yaha::SubscriptionMap subscriptions_;
    yaha::SubscriptionMap updatedSubscriptions_;
    yaha::PublishCallback publish_callback_{};
    std::size_t handled_messages_{0U};
};

struct TransportState {
    std::atomic<int> connect_calls{0};
    std::atomic<int> disconnect_calls{0};
    std::atomic<int> subscribe_calls{0};
    std::atomic<int> publish_calls{0};
    std::atomic<int> unsubscribe_calls{0};
    std::atomic<int> ping_calls{0};
    std::atomic<bool> connected{false};
    std::atomic<int> poll_throw_countdown{0};
    std::atomic<int> subscribe_fail_on_call{0};
    std::string publish_throw_reason{};
    std::deque<yaha::Message> inbox{};
    std::vector<std::pair<std::string, yaha::Qos>> subscriptions{};
    std::vector<yaha::Message> published_messages{};
};

yaha::YahaMqttClient::Transport makeTransport(TransportState& state) {
    yaha::YahaMqttClient::Transport transport{};

    transport.connect = [&state](const yaha::YahaMqttClient::Config&) {
        state.connect_calls.fetch_add(1);
        state.connected.store(true);
        return true;
    };

    transport.disconnect = [&state]() {
        state.disconnect_calls.fetch_add(1);
        state.connected.store(false);
    };

    transport.publish = [&state](const yaha::Message& message) {
        if (!state.publish_throw_reason.empty()) {
            throw std::runtime_error{state.publish_throw_reason};
        }
        state.publish_calls.fetch_add(1);
        state.published_messages.push_back(message);
    };

    transport.subscribe = [&state](const std::string& topic, yaha::Qos qos) {
        const int callNumber = state.subscribe_calls.fetch_add(1) + 1;
        if (state.subscribe_fail_on_call.load() == callNumber) {
            return false;
        }
        state.subscriptions.emplace_back(topic, qos);
        return true;
    };

    transport.unsubscribe = [&state](const std::string&) {
        state.unsubscribe_calls.fetch_add(1);
        return true;
    };

    transport.pollIncoming = [&state]() -> std::optional<yaha::Message> {
        if (state.poll_throw_countdown.load() > 0
            && state.poll_throw_countdown.fetch_sub(1) > 0) {
            throw std::runtime_error{"pollIncoming transient failure"};
        }
        if (state.inbox.empty()) {
            return std::nullopt;
        }
        yaha::Message message = state.inbox.front();
        state.inbox.pop_front();
        return message;
    };

    transport.ping = [&state]() {
        state.ping_calls.fetch_add(1);
    };

    transport.isConnected = [&state]() {
        return state.connected.load();
    };

    return transport;
}

} // namespace

TEST_CASE("run_injects_publish_callback_before_message_delivery", "[mqtt_client]") {
    TransportState state{};
    state.inbox.emplace_back("home/kitchen/state", std::string{"on"});

    RecordingComponent component{{{"home/+/state", yaha::Qos::AtLeastOnce}}};
    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.keepAliveInterval = std::chrono::milliseconds{100};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    client.close();

    REQUIRE(component.callbackSet());
    REQUIRE(component.callbackSetDuringHandle());
    REQUIRE(component.handledCount() == 1U);
}

TEST_CASE("connect_subscribes_all_component_filters", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{
        {"home/+/state", yaha::Qos::AtLeastOnce},
        {"sensor/#", yaha::Qos::AtMostOnce}
    }};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    client.close();

    REQUIRE(state.connect_calls.load() >= 1);
    REQUIRE(state.subscribe_calls.load() == 2);
}

TEST_CASE("disconnect_triggers_reconnect_and_resubscribe", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.reconnectDelay = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();

    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    state.connected.store(false);
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    client.close();

    REQUIRE(state.connect_calls.load() >= 2);
    REQUIRE(state.subscribe_calls.load() >= 2);
}

TEST_CASE("handled_inbound_message_resyncs_subscription_diff", "[mqtt_client]") {
    TransportState state{};
    state.inbox.emplace_back("home/kitchen/state", std::string{"sync"});

    MutatingSubscriptionsComponent component{{
        {"home/+/state", yaha::Qos::AtLeastOnce},
    }, {
        {"home/+/state", yaha::Qos::AtLeastOnce},
        {"sensor/#", yaha::Qos::AtMostOnce},
    }};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    client.close();

    REQUIRE(component.handledCount() == 1U);
    REQUIRE(state.subscribe_calls.load() == 2);
}

TEST_CASE("failed_subscribe_confirmation_keeps_filter_inactive", "[mqtt_client]") {
    TransportState state{};
    state.subscribe_fail_on_call.store(2);
    state.inbox.emplace_back("home/kitchen/state", std::string{"sync"});
    state.inbox.emplace_back("sensor/temp", std::string{"blocked"});

    MutatingSubscriptionsComponent component{{
        {"home/+/state", yaha::Qos::AtLeastOnce},
    }, {
        {"home/+/state", yaha::Qos::AtLeastOnce},
        {"sensor/#", yaha::Qos::AtMostOnce},
    }};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    client.close();

    REQUIRE(state.subscribe_calls.load() >= 2);
    REQUIRE(component.handledCount() == 1U);
}

TEST_CASE("publish_forwards_valid_message_to_transport", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    component.publishFromComponent(yaha::Message{"home/light", std::string{"on"}});
    client.close();

    REQUIRE(state.publish_calls.load() == 1);
    REQUIRE(state.published_messages.size() == 1U);
    REQUIRE(state.published_messages.front().topic() == "home/light");
}

TEST_CASE("publish_callback_maps_qos1_ack_timeout_to_publish_result", "[mqtt_client]") {
    TransportState state{};
    state.publish_throw_reason = "timed out waiting for PUBACK from broker";
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    const yaha::PublishResult publishResult = component.publishFromComponentWithResult(
        yaha::Message{"home/light", std::string{"on"}, yaha::Qos::AtLeastOnce, false});
    client.close();

    REQUIRE_FALSE(publishResult.success);
    REQUIRE(publishResult.category == yaha::PublishFailureCategory::AckTimeout);
    REQUIRE(publishResult.reason.find("PUBACK") != std::string::npos);
}

TEST_CASE("publish_callback_maps_qos2_ack_timeout_to_publish_result", "[mqtt_client]") {
    TransportState state{};
    state.publish_throw_reason = "timed out waiting for PUBREC from broker";
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    const yaha::PublishResult publishResult = component.publishFromComponentWithResult(
        yaha::Message{"home/light", 1.0, yaha::Qos::ExactlyOnce, false});
    client.close();

    REQUIRE_FALSE(publishResult.success);
    REQUIRE(publishResult.category == yaha::PublishFailureCategory::AckTimeout);
    REQUIRE(publishResult.reason.find("PUBREC") != std::string::npos);
}

TEST_CASE("inbound_non_matching_topic_is_filtered_out", "[mqtt_client]") {
    TransportState state{};
    state.inbox.emplace_back("other/topic", std::string{"x"});

    RecordingComponent component{{{"home/+/state", yaha::Qos::AtLeastOnce}}};
    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    client.close();

    REQUIRE(component.handledCount() == 0U);
}

TEST_CASE("keep_alive_ping_runs_while_connected", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.keepAliveInterval = std::chrono::milliseconds{10};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    client.close();

    REQUIRE(state.ping_calls.load() >= 1);
}

TEST_CASE("close_stops_loop_and_disconnects", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    client.close();

    REQUIRE_FALSE(client.isRunning());
    REQUIRE(state.disconnect_calls.load() == 1);
    REQUIRE(state.unsubscribe_calls.load() == 1);
}

TEST_CASE("transport_poll_exception_triggers_reconnect_without_crash", "[mqtt_client]") {
    TransportState state{};
    state.poll_throw_countdown.store(1);
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.reconnectDelay = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    client.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    client.close();

    REQUIRE(state.connect_calls.load() >= 2);
    REQUIRE(state.disconnect_calls.load() >= 1);
}

TEST_CASE("run_throws_when_transport_callbacks_are_missing", "[mqtt_client]") {
    RecordingComponent component{{{"home/#", yaha::Qos::AtLeastOnce}}};

    yaha::YahaMqttClient::Config config{};
    yaha::YahaMqttClient::Transport brokenTransport{};

    yaha::YahaMqttClient client{config, component, std::move(brokenTransport)};
    REQUIRE_THROWS_AS(client.run(), std::invalid_argument);
}

TEST_CASE("connect_trace_renders_qos2_subscription", "[mqtt_client]") {
    TransportState state{};
    RecordingComponent component{{{"home/qos2/#", yaha::Qos::ExactlyOnce}}};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.reconnectDelay = std::chrono::milliseconds{5};
    config.enableLifecycleTrace = true;
    config.enableMessageTrace = false;

    std::ostringstream captured{};
    auto* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    {
        yaha::YahaMqttClient client{config, component, makeTransport(state)};
        client.run();
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        client.close();
    }

    std::cout.rdbuf(oldBuffer);

    REQUIRE(state.subscribe_calls.load() == 1);
    REQUIRE(captured.str().find("qos=2") != std::string::npos);
}

TEST_CASE("message_trace_escapes_string_and_formats_numeric_values", "[mqtt_client]") {
    TransportState state{};
    state.inbox.emplace_back("home/trace/in", 21.5);

    RecordingComponent component{{{"home/trace/#", yaha::Qos::AtLeastOnce}}};
    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.reconnectDelay = std::chrono::milliseconds{5};
    config.enableLifecycleTrace = true;
    config.enableMessageTrace = true;

    std::ostringstream captured{};
    auto* oldBuffer = std::cout.rdbuf(captured.rdbuf());

    {
        yaha::YahaMqttClient client{config, component, makeTransport(state)};
        client.run();
        std::this_thread::sleep_for(std::chrono::milliseconds{30});

        yaha::Message outbound{"home/trace/out", std::string{"payload"}};
        outbound.setRawPayload("line1\n\"x\"\\tab\t");
        component.publishFromComponent(outbound);

        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        client.close();
    }

    std::cout.rdbuf(oldBuffer);

    const std::string output = captured.str();
    REQUIRE(output.find("mqtt: start clientId=") != std::string::npos);
    REQUIRE(output.find("mqtt: connected clientId=") != std::string::npos);
    REQUIRE(output.find("mqtt: incoming topic=home/trace/in") != std::string::npos);
    REQUIRE(output.find("value=21.5") != std::string::npos);
    REQUIRE(output.find("mqtt: outgoing topic=home/trace/out") != std::string::npos);
    REQUIRE(output.find("raw=\"line1\\n\\\"x\\\"\\\\tab\\t\"") != std::string::npos);
}

TEST_CASE("mqtt_client_runtime_run_until_signal_starts_and_stops_component",
          "[mqtt_client]") {
    class RuntimeRecordingComponent final : public yaha::IMqttComponent {
    public:
        [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
            return {};
        }

        void handleMessage([[maybe_unused]] const yaha::Message& message) override {
        }

        void setPublishCallback(yaha::PublishCallback callback) override {
            callback_ = std::move(callback);
        }

        void run() override {
            run_calls_.fetch_add(1);
        }

        void close() override {
            close_calls_.fetch_add(1);
        }

        [[nodiscard]] int runCalls() const {
            return run_calls_.load();
        }

        [[nodiscard]] int closeCalls() const {
            return close_calls_.load();
        }

    private:
        yaha::PublishCallback callback_{};
        std::atomic<int> run_calls_{0};
        std::atomic<int> close_calls_{0};
    };

    TransportState state{};
    RuntimeRecordingComponent component{};

    yaha::YahaMqttClient::Config config{};
    config.loopSleep = std::chrono::milliseconds{5};
    config.reconnectDelay = std::chrono::milliseconds{5};

    yaha::YahaMqttClient client{config, component, makeTransport(state)};
    yaha::YahaMqttClientRuntime runtime{client, component};

    std::thread runtime_thread([&runtime]() {
        runtime.runUntilSignal();
    });

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{500};
    while (!client.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    REQUIRE(client.isRunning());

    std::raise(SIGTERM);
    runtime_thread.join();

    CHECK_FALSE(client.isRunning());
    CHECK(component.runCalls() == 1);
    CHECK(component.closeCalls() == 1);
}
// NOLINTEND(readability-magic-numbers)