#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "data_model/message/message.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "outbound_queue/outbound_queue.h"

using namespace mqtt;

namespace {

Message make_message(const std::string &topic_name,
                     QoS qos = QoS::AtMostOnce) {
  Message msg;
  msg.topic = Utf8String{topic_name};
  msg.qos = qos;
  return msg;
}

} // namespace

TEST_CASE("outbound_queue_default_construction", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.is_empty());
  CHECK(queue.size() == 0U);
  CHECK_FALSE(queue.is_stopped());
}

TEST_CASE("outbound_queue_custom_max_depth", "[outbound_queue]") {
  OutboundQueue queue(5U);

  for (std::size_t idx = 0U; idx < 5U; ++idx) {
    CHECK(queue.push(make_message("topic/" + std::to_string(idx))));
  }
  CHECK(queue.size() == 5U);
  CHECK_FALSE(queue.push(make_message("topic/overflow")));
}

TEST_CASE("outbound_queue_push_and_try_pop", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.push(make_message("sensors/temp", QoS::AtLeastOnce)));
  CHECK(queue.size() == 1U);

  auto result = queue.try_pop();
  REQUIRE(result.has_value());
  CHECK(result->topic.value == "sensors/temp");
  CHECK(result->qos == QoS::AtLeastOnce);
  CHECK(queue.is_empty());
}

TEST_CASE("outbound_queue_fifo_order", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.push(make_message("msg/first")));
  CHECK(queue.push(make_message("msg/second")));
  CHECK(queue.push(make_message("msg/third")));

  auto first = queue.try_pop();
  REQUIRE(first.has_value());
  CHECK(first->topic.value == "msg/first");

  auto second = queue.try_pop();
  REQUIRE(second.has_value());
  CHECK(second->topic.value == "msg/second");

  auto third = queue.try_pop();
  REQUIRE(third.has_value());
  CHECK(third->topic.value == "msg/third");

  CHECK(queue.is_empty());
}

TEST_CASE("outbound_queue_try_pop_empty", "[outbound_queue]") {
  OutboundQueue queue;
  auto result = queue.try_pop();
  CHECK_FALSE(result.has_value());
}

TEST_CASE("outbound_queue_push_full_drops", "[outbound_queue]") {
  OutboundQueue queue(2U);
  CHECK(queue.push(make_message("msg/one")));
  CHECK(queue.push(make_message("msg/two")));
  CHECK(queue.size() == 2U);

  CHECK_FALSE(queue.push(make_message("msg/three")));
  CHECK(queue.size() == 2U);

  auto first = queue.try_pop();
  REQUIRE(first.has_value());
  CHECK(first->topic.value == "msg/one");
}

TEST_CASE("outbound_queue_stop_rejects_push", "[outbound_queue]") {
  OutboundQueue queue;
  queue.stop();
  CHECK(queue.is_stopped());
  CHECK_FALSE(queue.push(make_message("rejected")));
}

TEST_CASE("outbound_queue_stop_allows_drain", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.push(make_message("drain/me")));
  queue.stop();

  auto result = queue.try_pop();
  REQUIRE(result.has_value());
  CHECK(result->topic.value == "drain/me");
  CHECK(queue.is_empty());
}

TEST_CASE("outbound_queue_stop_idempotent", "[outbound_queue]") {
  OutboundQueue queue;
  queue.stop();
  queue.stop();
  CHECK(queue.is_stopped());
}

TEST_CASE("outbound_queue_size_tracks_push_pop", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.push(make_message("msg/a")));
  CHECK(queue.push(make_message("msg/b")));
  CHECK(queue.push(make_message("msg/c")));
  CHECK(queue.size() == 3U);

  (void)queue.try_pop();
  CHECK(queue.size() == 2U);
}

TEST_CASE("outbound_queue_is_empty_after_drain", "[outbound_queue]") {
  OutboundQueue queue;
  CHECK(queue.push(make_message("drain/first")));
  CHECK(queue.push(make_message("drain/second")));

  (void)queue.try_pop();
  (void)queue.try_pop();
  CHECK(queue.is_empty());
}
