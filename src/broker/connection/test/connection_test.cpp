#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "connection/outbound_queue_bridge.h"
#include "data_model/message/message.h"
#include "outbound_queue/outbound_queue.h"

using namespace mqtt;

TEST_CASE("drain_pending_outbound_messages_returns_fifo_and_empties_source",
          "[connection]") {
  OutboundQueue source_queue;

  Message first_message;
  first_message.topic.value = "topic/one";
  Message second_message;
  second_message.topic.value = "topic/two";

  REQUIRE(source_queue.push(first_message));
  REQUIRE(source_queue.push(second_message));

  const std::vector<Message> drained =
      drain_pending_outbound_messages(source_queue);

  REQUIRE(drained.size() == 2U);
  CHECK(drained[0].topic.value == "topic/one");
  CHECK(drained[1].topic.value == "topic/two");
  CHECK_FALSE(source_queue.try_pop().has_value());
}

TEST_CASE("transfer_pending_outbound_messages_moves_until_source_empty",
          "[connection]") {
  OutboundQueue source_queue;
  OutboundQueue target_queue;

  Message first_message;
  first_message.topic.value = "topic/one";
  Message second_message;
  second_message.topic.value = "topic/two";

  REQUIRE(source_queue.push(first_message));
  REQUIRE(source_queue.push(second_message));

  const std::size_t moved_count =
      transfer_pending_outbound_messages(source_queue, target_queue);

  CHECK(moved_count == 2U);
  CHECK_FALSE(source_queue.try_pop().has_value());

  const std::optional<Message> target_first = target_queue.try_pop();
  REQUIRE(target_first.has_value());
  CHECK(target_first->topic.value == "topic/one");
  const std::optional<Message> target_second = target_queue.try_pop();
  REQUIRE(target_second.has_value());
  CHECK(target_second->topic.value == "topic/two");
}

TEST_CASE("transfer_pending_outbound_messages_stops_when_target_rejects",
          "[connection]") {
  OutboundQueue source_queue;
  OutboundQueue target_queue(1U);

  Message first_message;
  first_message.topic.value = "topic/one";
  Message second_message;
  second_message.topic.value = "topic/two";
  Message third_message;
  third_message.topic.value = "topic/three";

  REQUIRE(source_queue.push(first_message));
  REQUIRE(source_queue.push(second_message));
  REQUIRE(source_queue.push(third_message));

  const std::size_t moved_count =
      transfer_pending_outbound_messages(source_queue, target_queue);

  CHECK(moved_count == 1U);

  const std::optional<Message> target_first = target_queue.try_pop();
  REQUIRE(target_first.has_value());
  CHECK(target_first->topic.value == "topic/one");

  const std::optional<Message> source_first_remaining = source_queue.try_pop();
  REQUIRE(source_first_remaining.has_value());
  CHECK(source_first_remaining->topic.value == "topic/three");
}
