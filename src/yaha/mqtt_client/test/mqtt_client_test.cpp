#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

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

struct TransportState {
    std::atomic<int> connect_calls{0};
    std::atomic<int> disconnect_calls{0};
    std::atomic<int> subscribe_calls{0};
    std::atomic<int> publish_calls{0};
    std::atomic<int> unsubscribe_calls{0};
    std::atomic<int> ping_calls{0};
    std::atomic<bool> connected{false};
    std::atomic<int> poll_throw_countdown{0};
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
        state.publish_calls.fetch_add(1);
        state.published_messages.push_back(message);
    };

    transport.subscribe = [&state](const std::string& topic, yaha::Qos qos) {
        state.subscribe_calls.fetch_add(1);
        state.subscriptions.emplace_back(topic, qos);
    };

    transport.unsubscribe = [&state](const std::string&) {
        state.unsubscribe_calls.fetch_add(1);
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

TEST_CASE("mqtt_client_runtime_run_until_signal_starts_and_stops_component",
          "[mqtt_client]") {
    class RuntimeRecordingComponent final : public yaha::IMqttComponent {
    public:
        [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
            return {};
        }

        void handleMessage(const yaha::Message&) override {
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
