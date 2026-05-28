#include "yaha/opensensemap/opensensemap_component.h"

#include <catch2/catch_test_macros.hpp>

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
    REQUIRE(published->topic() == "$SYS/opensensemap/success");
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
    REQUIRE(published->topic() == "$SYS/opensensemap/error");
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
    REQUIRE(published->topic() == "$SYS/opensensemap/error");
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
    REQUIRE(published->topic() == "$SYS/opensensemap/error");
    REQUIRE(std::get<double>(published->value()) == kStatusInternalServerError);
    REQUIRE(published->reason()[0].message.find("network down") != std::string::npos);
}
