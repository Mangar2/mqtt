#include "yaha/pushover/pushover_component.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kHttpStatusOk = 200;
constexpr double kStatusOk = 200.0;
constexpr double kStatusError = 500.0;
constexpr double kStatusUnprocessableEntity = 422.0;
constexpr double kPayloadValueStatusFailure = 3.14;

[[nodiscard]] yaha::PushoverConfig makeConfig() {
    return yaha::PushoverConfig{
        .token = "token-123",
        .user = "user-456",
        .devices = {"mobile-1", "mobile-2"},
        .subscriptions = {
            yaha::PushoverSubscriptionConfig{
                .topicFilter = "$MONITOR/incident/#",
                .qos = yaha::Qos::AtLeastOnce,
            },
            yaha::PushoverSubscriptionConfig{
                .topicFilter = "home/alert/#",
                .qos = yaha::Qos::AtMostOnce,
            },
        },
    };
}

} // namespace

TEST_CASE("subscriptions_include_all_configured_topic_filters", "[pushover]") {
    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk};
        }};

    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();
    REQUIRE(subscriptions.size() == 2U);
    REQUIRE(subscriptions.contains("$MONITOR/incident/#"));
    REQUIRE(subscriptions.contains("home/alert/#"));
    REQUIRE(subscriptions.at("$MONITOR/incident/#") == yaha::Qos::AtLeastOnce);
    REQUIRE(subscriptions.at("home/alert/#") == yaha::Qos::AtMostOnce);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("handle_message_posts_to_each_device_and_publishes_success_status", "[pushover]") {
    std::vector<yaha::Message> published{};
    std::vector<std::string> payloads{};

    yaha::PushoverComponent component{
        makeConfig(),
        [&payloads](const std::string& requestPath, const std::string& requestPayload) {
            REQUIRE(requestPath == "/1/messages.json");
            payloads.push_back(requestPayload);
            return yaha::PushoverHttpResult{
                .statusCode = kHttpStatusOk,
                .payload = R"({"status":1})",
                .contentType = "application/json; charset=UTF-8",
            };
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message);
        return yaha::PublishResult::ok();
    });
    component.run();

    yaha::Message input{"$MONITOR/incident/fire", std::string{"alert"}};
    input.addReason("critical fire alarm", "2026-05-28T10:00:00Z");
    component.handleMessage(input);

    REQUIRE(payloads.size() == 2U);
    REQUIRE(payloads[0].find("\"priority\":1") != std::string::npos);
    REQUIRE(payloads[0].find("\"device\":\"mobile-1\"") != std::string::npos);
    REQUIRE(payloads[1].find("\"device\":\"mobile-2\"") != std::string::npos);

    REQUIRE(published.size() == 2U);
    REQUIRE(published[0].topic() == "$MONITOR/pushover/success");
    REQUIRE(std::get<double>(published[0].value()) == kStatusOk);
    REQUIRE(published[1].topic() == "$MONITOR/pushover/success");
    REQUIRE(std::get<double>(published[1].value()) == kStatusOk);
}

TEST_CASE("handle_message_uses_default_priority_for_non_alert_values", "[pushover]") {
    std::optional<std::string> payload{};

    yaha::PushoverComponent component{
        makeConfig(),
        [&payload](const std::string&, const std::string& requestPayload) {
            payload = requestPayload;
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk, .payload = R"({"status":1})"};
        }};

    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"$MONITOR/incident/info", std::string{"warning"}});

    REQUIRE(payload.has_value());
    REQUIRE(payload->find("\"priority\":-1") != std::string::npos);
}

TEST_CASE("handle_message_publishes_error_when_sender_throws", "[pushover]") {
    std::optional<yaha::Message> published{};

    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) -> yaha::PushoverHttpResult {
            throw std::runtime_error{"network down"};
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        if (!published.has_value()) {
            published = message;
        }
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"$MONITOR/incident/fail", std::string{"alert"}});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/pushover/error");
    REQUIRE(std::get<double>(published->value()) == kStatusError);
}

TEST_CASE("handle_message_publishes_error_when_no_device_is_configured", "[pushover]") {
    yaha::PushoverConfig config = makeConfig();
    config.devices.clear();

    std::optional<yaha::Message> published{};
    yaha::PushoverComponent component{
        std::move(config),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk};
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"$MONITOR/incident/fail", std::string{"warning"}});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/pushover/error");
    REQUIRE(std::get<double>(published->value()) == kStatusUnprocessableEntity);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("handle_message_publishes_error_when_sender_callback_missing", "[pushover]") {
    std::optional<yaha::Message> published{};
    yaha::PushoverComponent component{makeConfig(), yaha::PushoverRequestSender{}};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    std::ostringstream capturedOutput{};
    std::streambuf* previousStderrBuffer = std::cerr.rdbuf(capturedOutput.rdbuf());

    component.handleMessage(yaha::Message{"$MONITOR/incident/warn", std::string{"warning"}});

    std::cerr.rdbuf(previousStderrBuffer);

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/pushover/error");
    REQUIRE(std::get<double>(published->value()) == kStatusError);
    REQUIRE(published->reason().front().message.find("callback is missing") != std::string::npos);

    const std::string logText = capturedOutput.str();
    REQUIRE(logText.find("component=\"pushover\" direction=\"incoming\"") != std::string::npos);
    REQUIRE(logText.find("topic=\"$MONITOR/incident/warn\"") != std::string::npos);
    REQUIRE(logText.find("reason=\"pushover request sender callback is missing\"") != std::string::npos);
}

TEST_CASE("handle_message_formats_error_payload_arrays_for_http_failure", "[pushover]") {
    std::optional<yaha::Message> published{};

    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{
                .statusCode = static_cast<int>(kStatusError),
                .payload = R"({"status":0,"errors":[" first ","second"]})",
                .contentType = "application/json",
            };
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        if (!published.has_value()) {
            published = message;
        }
        return yaha::PublishResult::ok();
    });
    component.run();

    std::ostringstream capturedOutput{};
    std::streambuf* previousStderrBuffer = std::cerr.rdbuf(capturedOutput.rdbuf());

    component.handleMessage(yaha::Message{"$MONITOR/incident/fail", std::string{"warning"}});

    std::cerr.rdbuf(previousStderrBuffer);

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/pushover/error");
    REQUIRE(published->reason().front().message.find("errors = [\" first \",\"second\"]") != std::string::npos);

    const std::string logText = capturedOutput.str();
    REQUIRE(logText.find("component=\"pushover\" direction=\"incoming\"") != std::string::npos);
    REQUIRE(logText.find("httpStatus=500") != std::string::npos);
}

TEST_CASE("handle_message_ignores_input_when_component_not_running", "[pushover]") {
    bool publishCalled = false;
    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk};
        }};

    component.setPublishCallback([&publishCalled](const yaha::Message&) {
        publishCalled = true;
        return yaha::PublishResult::ok();
    });

    component.handleMessage(yaha::Message{"$MONITOR/incident/fail", std::string{"warning"}});
    REQUIRE_FALSE(publishCalled);
}

TEST_CASE("component_close_stops_followup_processing", "[pushover]") {
    std::size_t publishedCount = 0U;
    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk, .payload = R"({"status":1})"};
        }};

    component.setPublishCallback([&publishedCount](const yaha::Message&) {
        publishedCount += 1U;
        return yaha::PublishResult::ok();
    });
    component.run();
    component.close();

    component.handleMessage(yaha::Message{"$MONITOR/incident/fail", std::string{"warning"}});
    REQUIRE(publishedCount == 0U);
}

TEST_CASE("handle_message_without_publish_callback_does_not_throw", "[pushover]") {
    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = kHttpStatusOk, .payload = R"({"status":1})"};
        }};

    component.run();
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$MONITOR/incident/fire", std::string{"alert"}}));
}

TEST_CASE("handle_message_logs_status_publish_failure_path", "[pushover]") {
    std::size_t publishCalls = 0U;
    yaha::PushoverComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::PushoverHttpResult{.statusCode = static_cast<int>(kStatusError), .payload = "{}", .contentType = "application/json"};
        }};

    component.setPublishCallback([&publishCalls](const yaha::Message&) {
        publishCalls += 1U;
        return yaha::PublishResult::fail(yaha::PublishFailureCategory::WriteFailed, "forced");
    });
    component.run();

    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"$MONITOR/incident/fire", kPayloadValueStatusFailure}));
    REQUIRE(publishCalls >= 1U);
}
