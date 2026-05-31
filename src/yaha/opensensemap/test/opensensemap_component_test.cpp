#include "yaha/opensensemap/opensensemap_component.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

constexpr int kHttpStatusCreated = 201;
constexpr double kPayloadValueTemperature = 23.5;
constexpr double kPayloadValueUnknown = 21.0;
constexpr double kStatusCreated = 201.0;
constexpr double kStatusNotFound = 404.0;
constexpr double kStatusUnprocessableEntity = 422.0;
constexpr double kStatusInternalServerError = 500.0;
constexpr double kPayloadValueOne = 1.0;
constexpr double kPayloadValueStatusFailure = 12.75;
constexpr int kUnknownExceptionCode = 42;
constexpr std::uint32_t kMinUploadIntervalSeconds = 60U;

[[nodiscard]] yaha::OpenSenseMapConfig makeConfig() {
    return yaha::OpenSenseMapConfig{
        .boxIdentifier = "box-4711",
        .subscribeQos = yaha::Qos::AtLeastOnce,
        .sensors = {
            yaha::OpenSenseMapSensorConfig{
                .sensorName = "temperature",
                .sensorUnit = "C",
                .topicFilter = "house/living/temperature",
                .sensorIdentifier = "sensor-temp",
            },
            yaha::OpenSenseMapSensorConfig{
                .sensorName = "humidity",
                .sensorUnit = "%",
                .topicFilter = "house/living/humidity",
                .sensorIdentifier = "sensor-humidity",
            },
        },
    };
}

} // namespace

TEST_CASE("subscriptions_include_all_configured_sensor_topics", "[opensensemap]") {
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    const yaha::SubscriptionMap subscriptions = component.getSubscriptions();
    REQUIRE(subscriptions.size() == 2U);
    REQUIRE(subscriptions.contains("house/living/temperature"));
    REQUIRE(subscriptions.contains("house/living/humidity"));
    REQUIRE(subscriptions.at("house/living/temperature") == yaha::Qos::AtLeastOnce);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("handle_message_publishes_success_status_for_http_201", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string& requestPath, const std::string& requestPayload) {
            REQUIRE(requestPath == "/boxes/box-4711/sensor-temp");
            REQUIRE(requestPayload.find("\"value\":23.5") != std::string::npos);
            return yaha::OpenSenseMapHttpResult{
                .statusCode = kHttpStatusCreated,
                .payload = R"({"message":"created"})",
                .contentType = "application/json; charset=UTF-8",
            };
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    yaha::Message input{"house/living/temperature", kPayloadValueTemperature};
    input.addReason("incoming test reason");
    component.handleMessage(input);

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/success");
    REQUIRE(std::holds_alternative<double>(published->value()));
    REQUIRE(std::get<double>(published->value()) == kStatusCreated);
    REQUIRE(published->reason().size() == 2U);
    REQUIRE(published->reason()[0].message.find("created") != std::string::npos);
    REQUIRE(published->reason()[1].message == "incoming test reason");
}

TEST_CASE("handle_message_publishes_error_when_sensor_mapping_missing", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/kitchen/temperature", kPayloadValueUnknown});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusNotFound);
    REQUIRE(published->reason().size() == 1U);
    REQUIRE(published->reason()[0].message.find("not found") != std::string::npos);
}

TEST_CASE("handle_message_publishes_error_for_non_numeric_value", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/living/temperature", std::string{"abc"}});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusUnprocessableEntity);
}

TEST_CASE("handle_message_publishes_error_when_sender_throws", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) -> yaha::OpenSenseMapHttpResult {
            throw std::runtime_error{"network down"};
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusInternalServerError);
    REQUIRE(published->reason()[0].message.find("network down") != std::string::npos);
}

TEST_CASE("handle_message_publishes_error_when_sender_callback_missing", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{makeConfig(), yaha::OpenSenseMapRequestSender{}};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusInternalServerError);
    REQUIRE(published->reason().front().message.find("callback is missing") != std::string::npos);
}

TEST_CASE("handle_message_ignores_input_when_component_not_running", "[opensensemap]") {
    bool publishCalled = false;
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.setPublishCallback([&publishCalled](const yaha::Message&) {
        publishCalled = true;
        return yaha::PublishResult::ok();
    });

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});
    REQUIRE_FALSE(publishCalled);
}

TEST_CASE("handle_message_publishes_error_for_unknown_exception", "[opensensemap]") {
    std::optional<yaha::Message> published{};
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) -> yaha::OpenSenseMapHttpResult {
            throw kUnknownExceptionCode;
        }};

    component.setPublishCallback([&published](const yaha::Message& message) {
        published = message;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});

    REQUIRE(published.has_value());
    REQUIRE(published->topic() == "$MONITOR/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusInternalServerError);
    REQUIRE(published->reason().front().message.find("unknown") != std::string::npos);
}

TEST_CASE("component_close_stops_followup_processing", "[opensensemap]") {
    std::size_t publishedCount = 0U;
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.setPublishCallback([&publishedCount](const yaha::Message&) {
        publishedCount += 1U;
        return yaha::PublishResult::ok();
    });
    component.run();
    component.close();

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});
    REQUIRE(publishedCount == 0U);
}

TEST_CASE("handle_message_without_publish_callback_does_not_throw", "[opensensemap]") {
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.run();
    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne}));
}

TEST_CASE("handle_message_logs_status_publish_failure_path", "[opensensemap]") {
    std::size_t publishCalls = 0U;
    yaha::OpenSenseMapComponent component{
        makeConfig(),
        [](const std::string&, const std::string&) {
            return yaha::OpenSenseMapHttpResult{.statusCode = static_cast<int>(kStatusInternalServerError), .payload = "backend error"};
        }};

    component.setPublishCallback([&publishCalls](const yaha::Message&) {
        publishCalls += 1U;
        return yaha::PublishResult::fail(yaha::PublishFailureCategory::WriteFailed, "forced");
    });
    component.run();

    REQUIRE_NOTHROW(component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueStatusFailure}));
    REQUIRE(publishCalls >= 1U);
}

TEST_CASE("handle_message_ignores_too_frequent_uploads_per_sensor", "[opensensemap]") {
    auto config = makeConfig();
    config.sensors[0].minUploadIntervalSeconds = kMinUploadIntervalSeconds;

    std::size_t requestCalls = 0U;
    std::size_t publishCalls = 0U;
    yaha::OpenSenseMapComponent component{
        std::move(config),
        [&requestCalls](const std::string&, const std::string&) {
            requestCalls += 1U;
            return yaha::OpenSenseMapHttpResult{.statusCode = kHttpStatusCreated};
        }};

    component.setPublishCallback([&publishCalls](const yaha::Message&) {
        publishCalls += 1U;
        return yaha::PublishResult::ok();
    });
    component.run();

    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});
    component.handleMessage(yaha::Message{"house/living/temperature", kPayloadValueOne});

    REQUIRE(requestCalls == 1U);
    REQUIRE(publishCalls == 1U);
}
