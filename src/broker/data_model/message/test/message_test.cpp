#include <catch2/catch_test_macros.hpp>

#include "data_model/message/message.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/types/qos.h"

using namespace mqtt;

// ── Message (1.5.1)
// ───────────────────────────────────────────────────────────

TEST_CASE("message_defaults", "[message]") {
  Message msg{};
  CHECK(msg.topic.value.empty());
  CHECK(msg.payload.data.empty());
  CHECK(msg.qos == QoS::AtMostOnce);
  CHECK(msg.retain == false);
  CHECK(msg.properties.empty());
}

TEST_CASE("message_set_fields", "[message]") {
  Message msg{};
  msg.topic.value = "sensors/temp";
  msg.payload.data = {0x01, 0x02, 0x03};
  msg.qos = QoS::AtLeastOnce;
  msg.retain = true;
  msg.properties.push_back(
      Property{.id = PropertyId::ContentType,
               .value = Utf8String{.value = "text/plain"}});

  CHECK(msg.topic.value == "sensors/temp");
  CHECK(msg.payload.data.size() == 3U);
  CHECK(msg.qos == QoS::AtLeastOnce);
  CHECK(msg.retain == true);
  CHECK(msg.properties.size() == 1U);
}

TEST_CASE("message_equality", "[message]") {
  Message lhs{};
  Message rhs{};
  CHECK(lhs == rhs);
}

TEST_CASE("message_inequality", "[message]") {
  Message lhs{};
  Message rhs{};
  rhs.retain = true;
  CHECK(lhs != rhs);
}

// ── WillMessage (1.5.2)
// ───────────────────────────────────────────────────────

TEST_CASE("will_message_defaults", "[message][will]") {
  WillMessage msg{};
  CHECK(msg.delay_interval == 0U);
  CHECK(msg.message.qos == QoS::AtMostOnce);
  CHECK(msg.message.retain == false);
  CHECK(msg.message.topic.value.empty());
}

TEST_CASE("will_message_with_delay", "[message][will]") {
  WillMessage msg{};
  msg.delay_interval = 30U;
  msg.message.topic.value = "client/will";
  msg.message.qos = QoS::ExactlyOnce;

  CHECK(msg.delay_interval == 30U);
  CHECK(msg.message.topic.value == "client/will");
  CHECK(msg.message.qos == QoS::ExactlyOnce);
}

TEST_CASE("will_message_equality", "[message][will]") {
  WillMessage lhs{};
  WillMessage rhs{};
  CHECK(lhs == rhs);
}

TEST_CASE("will_message_inequality", "[message][will]") {
  WillMessage lhs{};
  WillMessage rhs{};
  rhs.delay_interval = 60U;
  CHECK(lhs != rhs);
}
