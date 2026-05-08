#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

#include "yaha/message/message.h"

using yaha::Message;
using yaha::Qos;
using yaha::ReasonEntry;
using yaha::Value;

namespace {

constexpr double k_temperature_21_5{21.5};
constexpr double k_is_on_non_matching_value{2.0};

} // namespace

TEST_CASE("Message construction with string value", "[message]") {
    Message msg{"home/light", std::string{"on"}};

    REQUIRE(msg.topic() == "home/light");
    REQUIRE(std::get<std::string>(msg.value()) == "on");
    REQUIRE(msg.qos() == Qos::AtLeastOnce);
    REQUIRE_FALSE(msg.retain());
    REQUIRE(msg.reason().empty());
}

TEST_CASE("Message construction with double value", "[message]") {
    Message msg{"sensor/temp", k_temperature_21_5, Qos::ExactlyOnce, true};

    REQUIRE(msg.topic() == "sensor/temp");
    REQUIRE(std::get<double>(msg.value()) == k_temperature_21_5);
    REQUIRE(msg.qos() == Qos::ExactlyOnce);
    REQUIRE(msg.retain());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Message isOn string values", "[message]") {
    REQUIRE(Message{"t", std::string{"on"}}.isOn());
    REQUIRE(Message{"t", std::string{"ON"}}.isOn());
    REQUIRE(Message{"t", std::string{"true"}}.isOn());

    REQUIRE_FALSE(Message{"t", std::string{"off"}}.isOn());
    REQUIRE_FALSE(Message{"t", std::string{"false"}}.isOn());
    REQUIRE_FALSE(Message{"t", std::string{"1"}}.isOn());
    REQUIRE_FALSE(Message{"t", std::string{""}}.isOn());
}

TEST_CASE("Message isOn double values", "[message]") {
    REQUIRE(Message{"t", 1.0}.isOn());

    REQUIRE_FALSE(Message{"t", 0.0}.isOn());
    REQUIRE_FALSE(Message{"t", k_is_on_non_matching_value}.isOn());
    REQUIRE_FALSE(Message{"t", -1.0}.isOn());
}

TEST_CASE("Message addReason with explicit timestamp", "[message]") {
    Message msg{"t", std::string{"v"}};
    msg.addReason("first", "2024-01-01T00:00:00Z");

    REQUIRE(msg.reason().size() == 1U);
    REQUIRE(msg.reason()[0].message   == "first");
    REQUIRE(msg.reason()[0].timestamp == "2024-01-01T00:00:00Z");
}

TEST_CASE("Message addReason prepends, most recent at front", "[message]") {
    Message msg{"t", std::string{"v"}};
    msg.addReason("first",  "2024-01-01T00:00:00Z");
    msg.addReason("second", "2024-01-02T00:00:00Z");

    REQUIRE(msg.reason().size() == 2U);
    REQUIRE(msg.reason()[0].message == "second");
    REQUIRE(msg.reason()[1].message == "first");
}

TEST_CASE("Message addReason auto-generates timestamp", "[message]") {
    Message msg{"t", std::string{"v"}};
    msg.addReason("event");

    REQUIRE(msg.reason().size() == 1U);
    REQUIRE(msg.reason()[0].message.empty() == false);
    REQUIRE(msg.reason()[0].timestamp.size() >= 19U);
}

TEST_CASE("Message clone is independent copy", "[message]") {
    Message original{"t", std::string{"v"}};
    original.addReason("original reason", "2024-01-01T00:00:00Z");

    Message copy = original.clone();

    REQUIRE(copy.topic()  == original.topic());
    REQUIRE(copy.reason().size() == original.reason().size());

    copy.addReason("extra", "2024-06-01T00:00:00Z");

    REQUIRE(copy.reason().size()     == 2U);
    REQUIRE(original.reason().size() == 1U);
}

TEST_CASE("Message raw payload can be stored copied and cleared", "[message]") {
    Message original{"topic/raw", std::string{"payload"}};
    original.setRawPayload("{\"token\":\"abc\",\"message\":{\"topic\":\"topic/raw\",\"value\":\"payload\"}}");

    REQUIRE(original.rawPayload().has_value());
    REQUIRE(*original.rawPayload() ==
            "{\"token\":\"abc\",\"message\":{\"topic\":\"topic/raw\",\"value\":\"payload\"}}");

    Message cloned = original.clone();
    REQUIRE(cloned.rawPayload().has_value());
    REQUIRE(*cloned.rawPayload() == *original.rawPayload());

    cloned.clearRawPayload();
    REQUIRE_FALSE(cloned.rawPayload().has_value());
    REQUIRE(original.rawPayload().has_value());
}

TEST_CASE("Message validate passes for valid message", "[message]") {
    Message msg{"home/light", std::string{"on"}};
    REQUIRE_NOTHROW(Message::validate(msg));
}

TEST_CASE("Message validate rejects empty topic", "[message]") {
    Message msg{"", std::string{"on"}};
    REQUIRE_THROWS_AS(Message::validate(msg), std::invalid_argument);
}

TEST_CASE("Message validate rejects ReasonEntry with empty message", "[message]") {
    Message msg{"t", std::string{"v"}};
    msg.addReason("ok", "2024-01-01T00:00:00Z");
    msg.addReason("",   "2024-01-02T00:00:00Z");

    REQUIRE_THROWS_AS(Message::validate(msg), std::invalid_argument);
}

TEST_CASE("Message qos AtMostOnce construction", "[message]") {
    Message msg{"t", std::string{"v"}, Qos::AtMostOnce};
    REQUIRE(msg.qos() == Qos::AtMostOnce);
}
