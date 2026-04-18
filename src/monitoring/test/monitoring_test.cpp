/**
 * @file monitoring_test.cpp
 * @brief Unit tests for Module 16: Monitoring (StatisticsCollector +
 *        SysTopicPublisher).
 */

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "data_model/message/message.h"
#include "data_model/types/qos.h"
#include "monitoring/statistics_collector.h"
#include "monitoring/sys_topic_publisher.h"
#include "store/retained_message_store.h"
#include "store/subscription_store.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// StatisticsCollector
//

TEST_CASE("stats_initial_snapshot_is_zero", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  const auto snap = stats.snapshot();

  CHECK(snap.connected_clients == 0U);
  CHECK(snap.messages_inbound == 0U);
  CHECK(snap.messages_outbound == 0U);
  CHECK(snap.active_subscriptions == 0U);
  CHECK(snap.retained_messages == 0U);
  CHECK(snap.uptime.count() >= 0);
}

TEST_CASE("stats_client_connect_disconnect", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  SECTION("single connect increments counter") {
    stats.on_client_connected();
    CHECK(stats.snapshot().connected_clients == 1U);
  }

  SECTION("connect then disconnect returns to zero") {
    stats.on_client_connected();
    stats.on_client_connected();
    stats.on_client_disconnected();
    CHECK(stats.snapshot().connected_clients == 1U);
  }

  SECTION("multiple connects accumulate") {
    stats.on_client_connected();
    stats.on_client_connected();
    stats.on_client_connected();
    CHECK(stats.snapshot().connected_clients == 3U);
  }
}

TEST_CASE("stats_message_throughput", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  SECTION("inbound counter increments") {
    stats.on_message_inbound();
    stats.on_message_inbound();
    CHECK(stats.snapshot().messages_inbound == 2U);
  }

  SECTION("outbound counter increments") {
    stats.on_message_outbound();
    CHECK(stats.snapshot().messages_outbound == 1U);
  }

  SECTION("inbound and outbound are independent") {
    stats.on_message_inbound();
    stats.on_message_outbound();
    stats.on_message_outbound();
    const auto snap = stats.snapshot();
    CHECK(snap.messages_inbound == 1U);
    CHECK(snap.messages_outbound == 2U);
  }
}

TEST_CASE("stats_subscription_count_from_store", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  CHECK(stats.snapshot().active_subscriptions == 0U);

  Subscription sub;
  sub.topic_filter = Utf8String{"test/topic"};
  sub.qos = QoS::AtMostOnce;
  sub_store.store("client1", sub);

  CHECK(stats.snapshot().active_subscriptions == 1U);

  sub_store.store("client2", sub);
  CHECK(stats.snapshot().active_subscriptions == 2U);
}

TEST_CASE("stats_retained_count_from_store", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  CHECK(stats.snapshot().retained_messages == 0U);

  Message msg;
  msg.topic = Utf8String{"sensors/temp"};
  msg.payload = BinaryData{{0x31, 0x38}};
  msg.retain = true;
  retained_store.store(msg);

  CHECK(stats.snapshot().retained_messages == 1U);
}

TEST_CASE("stats_uptime_increases", "[monitoring]") {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats(sub_store, retained_store);

  // Uptime is non-negative immediately after construction.
  CHECK(stats.snapshot().uptime.count() >= 0);
}

//
// SysTopicPublisher
//

namespace {

/// Helper: build a store-backed StatisticsCollector with default empty stores.
struct TestFixture {
  SubscriptionStore sub_store;
  RetainedMessageStore retained_store;
  StatisticsCollector stats{sub_store, retained_store};
};

} // namespace

TEST_CASE("sys_publisher_zero_interval_no_publish", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(
      fix.stats, 0s, [&](Message msg) { published.push_back(std::move(msg)); });

  // Tick far in the future — should never publish with interval == 0.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK_FALSE(publisher.tick(far_future));
  CHECK(published.empty());
}

TEST_CASE("sys_publisher_first_tick_publishes", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  // First tick with now far in the future — interval (≥ epoch) is always
  // elapsed on the initial call since last_publish_ is the epoch.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK(publisher.tick(far_future));
  CHECK_FALSE(published.empty());
}

TEST_CASE("sys_publisher_interval_not_elapsed", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  const auto base = std::chrono::steady_clock::now() + 1000s;
  // First tick publishes.
  publisher.tick(base);
  published.clear();

  // Second tick only 1 second later — interval is 60 s, should not publish.
  CHECK_FALSE(publisher.tick(base + 1s));
  CHECK(published.empty());
}

TEST_CASE("sys_publisher_interval_elapsed", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  const auto base = std::chrono::steady_clock::now() + 1000s;
  publisher.tick(base);
  published.clear();

  // 60 seconds later — interval exactly elapsed, should publish.
  CHECK(publisher.tick(base + 60s));
  CHECK_FALSE(published.empty());
}

TEST_CASE("sys_publisher_publishes_all_sys_topics", "[monitoring]") {
  TestFixture fix;
  std::vector<std::string> topics;
  SysTopicPublisher publisher(
      fix.stats, 60s, [&](Message msg) { topics.push_back(msg.topic.value); });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  REQUIRE(topics.size() == 6U);
  CHECK(std::ranges::find(topics, "$SYS/broker/clients/connected") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/messages/received") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/messages/sent") != topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/subscriptions/count") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/retained messages/count") !=
        topics.end());
  CHECK(std::ranges::find(topics, "$SYS/broker/uptime") != topics.end());
}

TEST_CASE("sys_publisher_payload_is_decimal", "[monitoring]") {
  TestFixture fix;
  fix.stats.on_client_connected();
  fix.stats.on_client_connected();

  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  const auto connected_it =
      std::ranges::find_if(published, [](const Message &msg) {
        return msg.topic.value == "$SYS/broker/clients/connected";
      });
  REQUIRE(connected_it != published.end());

  const std::string payload(connected_it->payload.data.begin(),
                            connected_it->payload.data.end());
  CHECK(payload == "2");
}

TEST_CASE("sys_publisher_retain_flag_set", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  for (const auto &msg : published) {
    CHECK(msg.retain);
  }
}

TEST_CASE("sys_publisher_qos_at_most_once", "[monitoring]") {
  TestFixture fix;
  std::vector<Message> published;
  SysTopicPublisher publisher(fix.stats, 60s, [&](Message msg) {
    published.push_back(std::move(msg));
  });

  publisher.tick(std::chrono::steady_clock::now() + 1000s);

  for (const auto &msg : published) {
    CHECK(msg.qos == QoS::AtMostOnce);
  }
}
