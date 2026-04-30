#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "yaha/mqtt_component/mqtt_component.h"

namespace {

class SinkComponent final : public yaha::IMqttComponent {
public:
    [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
        return yaha::SubscriptionMap{
            {"home/temperature", yaha::Qos::AtLeastOnce},
            {"home/light", yaha::Qos::AtMostOnce}
        };
    }

    void handleMessage(const yaha::Message& message) override {
        received_messages_.push_back(message);
    }

    void run() override {}

    void close() override {}

    [[nodiscard]] std::size_t receivedCount() const noexcept {
        return received_messages_.size();
    }

    [[nodiscard]] const yaha::Message& lastMessage() const {
        return received_messages_.back();
    }

private:
    std::vector<yaha::Message> received_messages_;
};

class PublisherComponent final : public yaha::IMqttComponent {
public:
    [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
        return {};
    }

    void handleMessage(const yaha::Message& message) override {
        last_handled_ = message;
    }

    void setPublishCallback(yaha::PublishCallback callback) override {
        callback_ = std::move(callback);
    }

    void run() override {}

    void close() override {}

    void publish(const yaha::Message& message) const {
        if (callback_) {
            callback_(message);
        }
    }

private:
    yaha::Message last_handled_{"unused/topic", std::string{"unused"}};
    yaha::PublishCallback callback_{};
};

} // namespace

TEST_CASE("DefaultSetPublishCallback_NoThrow", "[mqtt_component]") {
    SinkComponent component{};
    REQUIRE_NOTHROW(component.setPublishCallback(
        [](const yaha::Message& /*message*/) {
        }));
}

TEST_CASE("PolymorphicHandleMessage_DispatchesToDerived", "[mqtt_component]") {
    SinkComponent component{};
    yaha::IMqttComponent& component_ref = component;

    yaha::Message message{"home/light", std::string{"on"}};
    component_ref.handleMessage(message);

    REQUIRE(component.receivedCount() == 1U);
    REQUIRE(component.lastMessage().topic() == "home/light");
    REQUIRE(std::get<std::string>(component.lastMessage().value()) == "on");
}

TEST_CASE("GetSubscriptions_ReturnsTopicToQosMap", "[mqtt_component]") {
    SinkComponent component{};

    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();

    REQUIRE(subscriptions.size() == 2U);
    REQUIRE(subscriptions.at("home/temperature") == yaha::Qos::AtLeastOnce);
    REQUIRE(subscriptions.at("home/light") == yaha::Qos::AtMostOnce);
}

TEST_CASE("OverriddenSetPublishCallback_CanPublishOutward", "[mqtt_component]") {
    PublisherComponent component{};
    std::vector<yaha::Message> published_messages{};

    component.setPublishCallback(
        [&published_messages](const yaha::Message& message) {
            published_messages.push_back(message);
        });

    component.publish(yaha::Message{"automation/door", std::string{"open"}});

    REQUIRE(published_messages.size() == 1U);
    REQUIRE(published_messages.front().topic() == "automation/door");
    REQUIRE(std::get<std::string>(published_messages.front().value()) == "open");
}
