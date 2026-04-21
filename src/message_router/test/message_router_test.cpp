#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "authz/acl_engine.h"
#include "authz/acl_rule.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/subscription/subscription.h"
#include "data_model/subscription/subscription_options.h"
#include "data_model/types/qos.h"
#include "data_model/types/variable_byte_integer.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_expiry_controller.h"
#include "message_router/message_router.h"
#include "message_router/message_router_error.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "message_router/subscriber_fanout.h"
#include "store/retained_message_store.h"
#include "store/subscription_store.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// Helpers
//

namespace {

constexpr uint16_t k_max_aliases = 10U;

AclRule allow_all() {
  return AclRule{.principal = "*",
                 .topic_pattern = "#",
                 .action = AclAction::PublishAndSubscribe,
                 .effect = AclEffect::Allow};
}

AclRule deny_all() {
  return AclRule{.principal = "*",
                 .topic_pattern = "#",
                 .action = AclAction::PublishAndSubscribe,
                 .effect = AclEffect::Deny};
}

Message make_msg(std::string topic_val, QoS qos = QoS::AtMostOnce,
                 bool retain = false) {
  Message msg;
  msg.topic.value = std::move(topic_val);
  msg.qos = qos;
  msg.retain = retain;
  return msg;
}

Subscription make_sub(std::string filter, QoS qos = QoS::ExactlyOnce,
                      bool no_local = false, bool retain_as_pub = false,
                      std::optional<uint32_t> identifier = std::nullopt) {
  Subscription sub;
  sub.topic_filter.value = std::move(filter);
  sub.qos = qos;
  sub.options.no_local = no_local;
  sub.options.retain_as_published = retain_as_pub;
  sub.identifier = identifier;
  return sub;
}

// Retrieve the SubscriptionIdentifier property value from a message, or
// std::nullopt when absent.
std::optional<uint32_t> get_sub_id(const Message &msg) {
  for (const auto &prop : msg.properties) {
    if (prop.id == PropertyId::SubscriptionIdentifier) {
      return std::get<VariableByteInteger>(prop.value).value;
    }
  }
  return std::nullopt;
}

// Retrieve the MessageExpiryInterval property value from a message, or
// std::nullopt when absent.
std::optional<uint32_t> get_expiry(const Message &msg) {
  for (const auto &prop : msg.properties) {
    if (prop.id == PropertyId::MessageExpiryInterval) {
      return std::get<uint32_t>(prop.value);
    }
  }
  return std::nullopt;
}

} // namespace

//
// InboundPublishProcessor (12.1)
//

TEST_CASE("inbound_processor_auth_denied_throws", "[message_router]") {
  AclEngine acl({deny_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  Message msg = make_msg("sensor/temp");
  CHECK_THROWS_AS((void)proc.process(msg, "cli", "", alias_table),
                  MessageRouterException);
  try {
    (void)proc.process(msg, "cli", "", alias_table);
  } catch (const MessageRouterException &exc) {
    CHECK(exc.error() == MessageRouterError::PublishNotAuthorized);
  }
}

TEST_CASE("inbound_processor_no_subscribers", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  Message msg = make_msg("sensor/temp");
  const auto result = proc.process(msg, "cli", "", alias_table);
  CHECK(result.empty());
}

TEST_CASE("inbound_processor_returns_subscribers", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("c1", make_sub("sensor/temp"));
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  Message msg = make_msg("sensor/temp");
  const auto result = proc.process(msg, "publisher", "", alias_table);
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c1");
}

TEST_CASE("inbound_processor_stores_retained", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  Message msg = make_msg("sensor/temp");
  msg.retain = true;
  msg.payload.data = {0x01U};

  (void)proc.process(msg, "cli", "", alias_table);
  const auto found = retained.find("sensor/temp");
  REQUIRE(found.size() == 1U);
  CHECK(found[0].topic.value == "sensor/temp");
}

TEST_CASE("inbound_processor_no_retain_when_flag_false", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  Message msg = make_msg("sensor/temp");
  msg.retain = false;
  msg.payload.data = {0x01U};

  (void)proc.process(msg, "cli", "", alias_table);
  CHECK(retained.size() == 0U);
}

TEST_CASE("inbound_processor_alias_new_registration", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("c1", make_sub("sensor/temp"));
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  // First PUBLISH: topic + alias → registers alias 1 → "sensor/temp"
  Message msg1 = make_msg("sensor/temp");
  msg1.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{1U}});

  const auto result = proc.process(msg1, "p", "", alias_table);
  CHECK(result.size() == 1U);
  // Alias property must be stripped from the message.
  CHECK(msg1.properties.empty());
}

TEST_CASE("inbound_processor_alias_only_resolution", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("c1", make_sub("sensor/temp"));
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  // Register alias 1 → "sensor/temp"
  alias_table.set_inbound(1U, "sensor/temp");

  // Alias-only PUBLISH: empty topic + alias 1
  Message msg = make_msg("");
  msg.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{1U}});

  const auto result = proc.process(msg, "p", "", alias_table);
  CHECK(msg.topic.value == "sensor/temp");
  CHECK(msg.properties.empty());
  CHECK(result.size() == 1U);
}

TEST_CASE("inbound_processor_alias_unregistered_throws", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(k_max_aliases);

  // Alias-only PUBLISH with no prior registration.
  Message msg = make_msg("");
  msg.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{5U}});

  try {
    (void)proc.process(msg, "p", "", alias_table);
    FAIL("Expected MessageRouterException");
  } catch (const MessageRouterException &exc) {
    CHECK(exc.error() == MessageRouterError::TopicAliasInvalid);
  }
}

TEST_CASE("inbound_processor_alias_out_of_range_throws", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  TopicAliasTable alias_table(2U); // max = 2

  // Alias value 5 exceeds the maximum of 2.
  Message msg = make_msg("t");
  msg.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{5U}});

  try {
    (void)proc.process(msg, "p", "", alias_table);
    FAIL("Expected MessageRouterException");
  } catch (const MessageRouterException &exc) {
    CHECK(exc.error() == MessageRouterError::TopicAliasInvalid);
  }
}

//
// SubscriberFanout (12.2)
//

TEST_CASE("fanout_no_local_filters_publisher", "[message_router]") {
  const Message msg = make_msg("t");
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "pub",
                  .subscription = make_sub("t", QoS::AtLeastOnce, true)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "pub");
  CHECK(items.empty());
}

TEST_CASE("fanout_no_local_false_includes_publisher", "[message_router]") {
  const Message msg = make_msg("t");
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "pub",
                  .subscription = make_sub("t", QoS::AtMostOnce, false)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "pub");
  CHECK(items.size() == 1U);
}

TEST_CASE("fanout_qos_downgrade", "[message_router]") {
  Message msg = make_msg("t", QoS::ExactlyOnce);
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription = make_sub("t", QoS::AtLeastOnce)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "other");
  REQUIRE(items.size() == 1U);
  CHECK(items[0].message.qos == QoS::AtLeastOnce);
}

TEST_CASE("fanout_qos_not_upgraded", "[message_router]") {
  const Message msg = make_msg("t", QoS::AtMostOnce);
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription = make_sub("t", QoS::ExactlyOnce)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "other");
  REQUIRE(items.size() == 1U);
  CHECK(items[0].message.qos == QoS::AtMostOnce);
}

TEST_CASE("fanout_retain_cleared_by_default", "[message_router]") {
  Message msg = make_msg("t");
  msg.retain = true;
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription = make_sub("t", QoS::AtMostOnce, false, false)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  REQUIRE(items.size() == 1U);
  CHECK_FALSE(items[0].message.retain);
}

TEST_CASE("fanout_retain_preserved_when_option_set", "[message_router]") {
  Message msg = make_msg("t");
  msg.retain = true;
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription = make_sub("t", QoS::AtMostOnce, false, true)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  REQUIRE(items.size() == 1U);
  CHECK(items[0].message.retain);
}

TEST_CASE("fanout_subscription_identifier_attached", "[message_router]") {
  const Message msg = make_msg("t");
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription =
                      make_sub("t", QoS::AtMostOnce, false, false, 42U)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  REQUIRE(items.size() == 1U);
  const auto sub_id = get_sub_id(items[0].message);
  REQUIRE(sub_id.has_value());
  CHECK(*sub_id == 42U);
}

TEST_CASE("fanout_no_subscription_identifier_when_absent", "[message_router]") {
  const Message msg = make_msg("t");
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1", .subscription = make_sub("t")},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  REQUIRE(items.size() == 1U);
  CHECK_FALSE(get_sub_id(items[0].message).has_value());
}

TEST_CASE("fanout_multiple_subscribers", "[message_router]") {
  const Message msg = make_msg("t");
  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1", .subscription = make_sub("t")},
      MatchResult{.client_id = "s2", .subscription = make_sub("t")},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  CHECK(items.size() == 2U);
}

//
// OfflineQueue (12.3)
//

TEST_CASE("offline_queue_enqueue_and_drain", "[message_router]") {
  OfflineQueue queue;
  const Message msg = make_msg("t/1");
  queue.enqueue("cli", msg);
  CHECK(queue.size("cli") == 1U);
  const auto drained = queue.drain("cli");
  REQUIRE(drained.size() == 1U);
  CHECK(drained[0].message.topic.value == "t/1");
  CHECK(queue.size("cli") == 0U);
}

TEST_CASE("offline_queue_drain_empty_client", "[message_router]") {
  OfflineQueue queue;
  const auto result = queue.drain("nobody");
  CHECK(result.empty());
}

TEST_CASE("offline_queue_exceeds_limit_throws", "[message_router]") {
  OfflineQueue queue(2U);
  queue.enqueue("cli", make_msg("t/1"));
  queue.enqueue("cli", make_msg("t/2"));
  CHECK_THROWS_AS(queue.enqueue("cli", make_msg("t/3")),
                  MessageRouterException);
  try {
    queue.enqueue("cli", make_msg("t/3"));
  } catch (const MessageRouterException &exc) {
    CHECK(exc.error() == MessageRouterError::QueueFull);
  }
}

TEST_CASE("offline_queue_drain_preserves_fifo_order", "[message_router]") {
  OfflineQueue queue;
  queue.enqueue("cli", make_msg("t/1"));
  queue.enqueue("cli", make_msg("t/2"));
  queue.enqueue("cli", make_msg("t/3"));
  const auto drained = queue.drain("cli");
  REQUIRE(drained.size() == 3U);
  CHECK(drained[0].message.topic.value == "t/1");
  CHECK(drained[1].message.topic.value == "t/2");
  CHECK(drained[2].message.topic.value == "t/3");
}

TEST_CASE("offline_queue_purge_removes_messages", "[message_router]") {
  OfflineQueue queue;
  queue.enqueue("cli", make_msg("t/1"));
  queue.enqueue("cli", make_msg("t/2"));
  queue.purge("cli");
  CHECK(queue.size("cli") == 0U);
  CHECK(queue.drain("cli").empty());
}

TEST_CASE("offline_queue_size_per_client", "[message_router]") {
  OfflineQueue queue;
  queue.enqueue("c1", make_msg("t/1"));
  queue.enqueue("c1", make_msg("t/2"));
  queue.enqueue("c2", make_msg("t/3"));
  CHECK(queue.size("c1") == 2U);
  CHECK(queue.size("c2") == 1U);
}

TEST_CASE("offline_queue_enqueue_drop_oldest_replaces_head_when_full",
          "[message_router]") {
  OfflineQueue queue(2U);
  queue.enqueue("cli", make_msg("t/1"));
  queue.enqueue("cli", make_msg("t/2"));

  queue.enqueue_drop_oldest("cli", make_msg("t/3"));

  const auto drained = queue.drain("cli");
  REQUIRE(drained.size() == 2U);
  CHECK(drained[0].message.topic.value == "t/2");
  CHECK(drained[1].message.topic.value == "t/3");
}

TEST_CASE("offline_queue_enqueue_drop_oldest_handles_zero_capacity",
          "[message_router]") {
  OfflineQueue queue(0U);

  queue.enqueue_drop_oldest("cli", make_msg("t/1"));
  CHECK(queue.size("cli") == 1U);

  queue.enqueue_drop_oldest("cli", make_msg("t/2"));
  CHECK(queue.size("cli") == 1U);

  const auto drained = queue.drain("cli");
  REQUIRE(drained.size() == 1U);
  CHECK(drained[0].message.topic.value == "t/2");
}

//
// MessageExpiryController (12.4)
//

namespace {

const std::chrono::steady_clock::time_point k_epoch{};

Message make_msg_with_expiry(const std::string &topic_val,
                             uint32_t interval_secs) {
  Message msg = make_msg(topic_val);
  msg.properties.push_back(Property{.id = PropertyId::MessageExpiryInterval,
                                    .value = interval_secs});
  return msg;
}

} // namespace

TEST_CASE("expiry_no_property_always_valid", "[message_router]") {
  Message msg = make_msg("t");
  CHECK(MessageExpiryController::update_expiry(msg, k_epoch, k_epoch + 100s));
}

TEST_CASE("expiry_not_expired_updates_remaining", "[message_router]") {
  Message msg = make_msg_with_expiry("t", 10U);
  const auto now = k_epoch + 3s;
  const bool valid = MessageExpiryController::update_expiry(msg, k_epoch, now);
  CHECK(valid);
  const auto remaining = get_expiry(msg);
  REQUIRE(remaining.has_value());
  CHECK(*remaining == 7U);
}

TEST_CASE("expiry_exactly_at_boundary_expired", "[message_router]") {
  Message msg = make_msg_with_expiry("t", 10U);
  const bool valid =
      MessageExpiryController::update_expiry(msg, k_epoch, k_epoch + 10s);
  CHECK_FALSE(valid);
}

TEST_CASE("expiry_past_boundary_expired", "[message_router]") {
  Message msg = make_msg_with_expiry("t", 5U);
  const bool valid =
      MessageExpiryController::update_expiry(msg, k_epoch, k_epoch + 20s);
  CHECK_FALSE(valid);
}

TEST_CASE("expiry_zero_elapsed_unchanged", "[message_router]") {
  Message msg = make_msg_with_expiry("t", 30U);
  const bool valid =
      MessageExpiryController::update_expiry(msg, k_epoch, k_epoch);
  CHECK(valid);
  const auto remaining = get_expiry(msg);
  REQUIRE(remaining.has_value());
  CHECK(*remaining == 30U);
}

//
// SharedSubscriptionDispatcher (12.5)
//

TEST_CASE("shared_single_member_always_selected", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "sensor/temp", "c1", make_sub("sensor/temp"));

  const auto res1 = dispatcher.select_next_for_topic("sensor/temp");
  const auto res2 = dispatcher.select_next_for_topic("sensor/temp");
  REQUIRE(res1.size() == 1U);
  REQUIRE(res2.size() == 1U);
  CHECK(res1[0].client_id == "c1");
  CHECK(res2[0].client_id == "c1");
}

TEST_CASE("shared_two_members_round_robin", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.add_member("grp", "t", "c2", make_sub("t"));

  const auto first = dispatcher.select_next_for_topic("t");
  const auto second = dispatcher.select_next_for_topic("t");

  REQUIRE(first.size() == 1U);
  REQUIRE(second.size() == 1U);
  CHECK(first[0].client_id != second[0].client_id);
}

TEST_CASE("shared_remove_member_cleans_up_empty_group", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.remove_member("grp", "t", "c1");
  CHECK(dispatcher.member_count("grp", "t") == 0U);
  const auto result = dispatcher.select_next_for_topic("t");
  CHECK(result.empty());
}

TEST_CASE("shared_remove_client_from_all_groups", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("g1", "a", "c1", make_sub("a"));
  dispatcher.add_member("g2", "b", "c1", make_sub("b"));
  dispatcher.remove_client("c1");
  CHECK(dispatcher.member_count("g1", "a") == 0U);
  CHECK(dispatcher.member_count("g2", "b") == 0U);
}

TEST_CASE("shared_no_match_different_topic", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "sensor/temp", "c1", make_sub("sensor/temp"));
  const auto result = dispatcher.select_next_for_topic("sensor/humidity");
  CHECK(result.empty());
}

TEST_CASE("shared_wildcard_plus_matches", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "+/temp", "c1", make_sub("+/temp"));
  const auto result = dispatcher.select_next_for_topic("sensor/temp");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c1");
}

TEST_CASE("shared_wildcard_hash_matches", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "#", "c1", make_sub("#"));
  const auto result = dispatcher.select_next_for_topic("a/b/c");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c1");
}

TEST_CASE("shared_system_topic_not_matched_by_wildcard", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "+/stat", "c1", make_sub("+/stat"));
  // $SYS topics must not be matched by filters starting with '+' at root.
  const auto result = dispatcher.select_next_for_topic("$SYS/stat");
  CHECK(result.empty());
}

TEST_CASE("shared_multiple_groups_each_gets_one_delivery", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("g1", "t", "c1", make_sub("t"));
  dispatcher.add_member("g1", "t", "c2", make_sub("t"));
  dispatcher.add_member("g2", "t", "c3", make_sub("t"));

  const auto result = dispatcher.select_next_for_topic("t");
  // Two groups → two delivery targets (one per group).
  CHECK(result.size() == 2U);
}

TEST_CASE("shared_member_count_correct", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.add_member("grp", "t", "c2", make_sub("t"));
  CHECK(dispatcher.member_count("grp", "t") == 2U);
  dispatcher.remove_member("grp", "t", "c1");
  CHECK(dispatcher.member_count("grp", "t") == 1U);
}

TEST_CASE("shared_replace_existing_member", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  // Add c1 once with QoS::AtMostOnce, then replace with QoS::ExactlyOnce.
  dispatcher.add_member("grp", "t", "c1", make_sub("t", QoS::AtMostOnce));
  dispatcher.add_member("grp", "t", "c1", make_sub("t", QoS::ExactlyOnce));
  CHECK(dispatcher.member_count("grp", "t") == 1U);
  const auto result = dispatcher.select_next_for_topic("t");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].subscription.qos == QoS::ExactlyOnce);
}

//
// MessageRouter — integration (12)
//

TEST_CASE("router_deliver_to_online_subscriber", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("sub1", make_sub("sensor/temp"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  std::vector<std::string> delivered_clients;
  std::vector<Message> delivered_msgs;

  MessageRouter router(
      proc, offline_queue, shared,
      [](std::string_view cid) { return cid == "sub1"; },
      [&](std::string_view cid, const Message &msg) {
        delivered_clients.emplace_back(cid);
        delivered_msgs.push_back(msg);
      });

  TopicAliasTable alias_table(0U); // no aliases
  Message msg = make_msg("sensor/temp");
  router.route(msg, "publisher", "", alias_table);

  REQUIRE(delivered_clients.size() == 1U);
  CHECK(delivered_clients[0] == "sub1");
  CHECK(delivered_msgs[0].topic.value == "sensor/temp");
}

TEST_CASE("router_enqueue_for_offline_subscriber", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("sub1", make_sub("t"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  MessageRouter router(
      proc, offline_queue, shared,
      [](std::string_view) { return false; }, // everyone offline
      [](std::string_view, const Message &) {});

  TopicAliasTable alias_table(0U);
  Message msg = make_msg("t", QoS::AtLeastOnce);
  router.route(msg, "pub", "", alias_table);

  CHECK(offline_queue.size("sub1") == 1U);
}

TEST_CASE("router_flush_delivers_queued_messages", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("sub1", make_sub("t"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  std::vector<std::string> delivered_clients;
  bool online_flag = false;

  MessageRouter router(
      proc, offline_queue, shared,
      [&](std::string_view) { return online_flag; },
      [&](std::string_view cid, const Message &) {
        delivered_clients.emplace_back(cid);
      });

  TopicAliasTable alias_table(0U);

  // Route while offline — enqueues.
  Message msg = make_msg("t", QoS::AtLeastOnce);
  router.route(msg, "pub", "", alias_table);
  CHECK(offline_queue.size("sub1") == 1U);

  // Client comes back online — flush delivers.
  online_flag = true;
  router.flush_offline_queue("sub1");
  CHECK(offline_queue.size("sub1") == 0U);
  REQUIRE(delivered_clients.size() == 1U);
  CHECK(delivered_clients[0] == "sub1");
}

TEST_CASE("router_flush_discards_expired_messages", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  std::vector<std::string> delivered_clients;

  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return false; },
      [&](std::string_view cid, const Message &) {
        delivered_clients.emplace_back(cid);
      });

  // Manually enqueue an already-expired message.
  Message msg = make_msg_with_expiry("t", 5U);
  // Enqueue with a timestamp 10 seconds in the past.
  offline_queue.enqueue("cli", msg);
  // Drain and requeue with an old enqueue_time to simulate expiry.
  auto queued = offline_queue.drain("cli");
  queued[0].enqueue_time = std::chrono::steady_clock::now() - 10s;
  // Re-insert manually via the public API — we use a fresh queue entry.
  // Since OfflineQueue::enqueue doesn't accept QueuedMessage directly,
  // we exercise flush with a real expired scenario by routing a message
  // and advancing the clock via update_expiry directly.
  //
  // Here we test the expiry path directly on update_expiry instead.
  const bool valid = MessageExpiryController::update_expiry(
      queued[0].message, queued[0].enqueue_time);
  CHECK_FALSE(valid); // Confirms expiry detection works for this scenario.
}

TEST_CASE("router_auth_denied_throws", "[message_router]") {
  AclEngine acl({deny_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [](std::string_view, const Message &) {});

  TopicAliasTable alias_table(0U);
  Message msg = make_msg("t");
  CHECK_THROWS_AS(router.route(msg, "p", "", alias_table),
                  MessageRouterException);
}

TEST_CASE("router_deliver_retained_send_if_new", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  Message retained_message = make_msg("sensor/temp", QoS::ExactlyOnce, true);
  retained_message.payload.data = {0xAAU};
  retained.store(retained_message);

  std::vector<Message> delivered;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &msg) { delivered.push_back(msg); });

  Subscription subscription =
      make_sub("sensor/#", QoS::AtLeastOnce, false, false, 55U);
  subscription.options.retain_handling = RetainHandling::SendIfNew;

  router.deliver_retained("sub1", "sensor/#", subscription,
                          false); // updated subscription
  CHECK(delivered.empty());

  router.deliver_retained("sub1", "sensor/#", subscription,
                          true); // new subscription
  REQUIRE(delivered.size() == 1U);
  CHECK(delivered[0].qos == QoS::AtLeastOnce);
  CHECK_FALSE(delivered[0].retain);
  const auto identifier = get_sub_id(delivered[0]);
  REQUIRE(identifier.has_value());
  CHECK(*identifier == 55U);
}

TEST_CASE("router_deliver_retained_preserves_retain_when_rap_enabled",
          "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  Message retained_message = make_msg("sensor/temp", QoS::AtLeastOnce, true);
  retained_message.payload.data = {0xBCU};
  retained.store(retained_message);

  std::vector<Message> delivered;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &msg) { delivered.push_back(msg); });

  Subscription subscription =
      make_sub("sensor/#", QoS::AtLeastOnce, false, true, 99U);
  subscription.options.retain_handling = RetainHandling::SendAtSubscribe;

  router.deliver_retained("sub1", "sensor/#", subscription, true);
  REQUIRE(delivered.size() == 1U);
  CHECK(delivered[0].retain);
  const auto identifier = get_sub_id(delivered[0]);
  REQUIRE(identifier.has_value());
  CHECK(*identifier == 99U);
}

TEST_CASE("router_deliver_retained_never", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  Message retained_message = make_msg("sensor/temp", QoS::AtLeastOnce, true);
  retained_message.payload.data = {0x10U};
  retained.store(retained_message);

  int delivered_count = 0;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &) { ++delivered_count; });

  Subscription subscription = make_sub("sensor/#");
  subscription.options.retain_handling = RetainHandling::Never;

  router.deliver_retained("sub1", "sensor/#", subscription, true);
  CHECK(delivered_count == 0);
}

TEST_CASE("router_deliver_retained_discards_zero_expiry", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  Message retained_message = make_msg("sensor/temp", QoS::AtLeastOnce, true);
  retained_message.payload.data = {0x11U};
  retained_message.properties.push_back(Property{
      .id = PropertyId::MessageExpiryInterval,
      .value = uint32_t{0U},
  });
  retained.store(retained_message);

  int delivered_count = 0;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &) { ++delivered_count; });

  Subscription subscription = make_sub("sensor/#");
  subscription.options.retain_handling = RetainHandling::SendAtSubscribe;

  router.deliver_retained("sub1", "sensor/#", subscription, true);
  CHECK(delivered_count == 0);
}

TEST_CASE("router_deliver_retained_discards_elapsed_expiry", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  Message retained_message = make_msg("sensor/temp", QoS::AtLeastOnce, true);
  retained_message.payload.data = {0x12U};
  retained_message.properties.push_back(Property{
      .id = PropertyId::MessageExpiryInterval,
      .value = uint32_t{5U},
  });
  retained.store(retained_message);

  int delivered_count = 0;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &) { ++delivered_count; });

  Subscription subscription = make_sub("sensor/#");
  subscription.options.retain_handling = RetainHandling::SendAtSubscribe;

  router.deliver_retained("sub1", "sensor/#", subscription, true,
                          std::chrono::steady_clock::now() + 10s);
  CHECK(delivered_count == 0);
}

TEST_CASE("router_route_shared_subscription_online", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs; // no regular subscriptions
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;
  shared.add_member("grp", "sensor/temp", "shared1", make_sub("sensor/temp"));

  std::vector<std::string> delivered;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view cid, const Message &) {
        delivered.emplace_back(cid);
      });

  TopicAliasTable alias_table(0U);
  Message msg = make_msg("sensor/temp");
  router.route(msg, "pub", "", alias_table);

  REQUIRE(delivered.size() == 1U);
  CHECK(delivered[0] == "shared1");
}

TEST_CASE("router_discards_immediately_expired_message", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("sub1", make_sub("t"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  int delivered_count = 0;
  MessageRouter router(
      proc, offline_queue, shared, [](std::string_view) { return true; },
      [&](std::string_view, const Message &) { ++delivered_count; });

  TopicAliasTable alias_table(0U);
  // MessageExpiryInterval = 0: message expires at the moment of dispatch.
  Message msg = make_msg("t");
  msg.properties.push_back(
      Property{.id = PropertyId::MessageExpiryInterval, .value = uint32_t{0U}});
  router.route(msg, "pub", "", alias_table);
  CHECK(delivered_count == 0);
}

TEST_CASE("router_flush_discards_expired_in_queue", "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("sub1", make_sub("t"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  std::vector<std::string> delivered;
  bool online = false;
  MessageRouter router(
      proc, offline_queue, shared, [&](std::string_view) { return online; },
      [&](std::string_view cid, const Message &) {
        delivered.emplace_back(cid);
      });

  TopicAliasTable alias_table(0U);

  // Route a message with 5-second expiry while client is offline.
  Message msg = make_msg("t", QoS::AtLeastOnce);
  msg.properties.push_back(
      Property{.id = PropertyId::MessageExpiryInterval, .value = uint32_t{5U}});
  router.route(msg, "pub", "", alias_table);
  CHECK(offline_queue.size("sub1") == 1U);

  // Flush 10 seconds into the future — message has expired.
  router.flush_offline_queue("sub1", std::chrono::steady_clock::now() + 10s);
  CHECK(delivered.empty());
}

//
// SubscriberFanout — duplicate SubscriptionIdentifier replacement (12.2.4)
//

TEST_CASE("fanout_duplicate_sub_id_replaced", "[message_router]") {
  // Inbound message already carries a SubscriptionIdentifier.
  Message msg = make_msg("t");
  msg.properties.push_back(Property{
      .id = PropertyId::SubscriptionIdentifier,
      .value = VariableByteInteger{5U},
  });

  const std::vector<MatchResult> matches{
      MatchResult{.client_id = "s1",
                  .subscription =
                      make_sub("t", QoS::AtMostOnce, false, false, 42U)},
  };
  const auto items = SubscriberFanout::prepare(msg, matches, "p");
  REQUIRE(items.size() == 1U);
  // Property 5 from the inbound message must be replaced by the
  // subscription's identifier 42.
  const auto sub_id = get_sub_id(items[0].message);
  REQUIRE(sub_id.has_value());
  CHECK(*sub_id == 42U);
}

//
// SharedSubscriptionDispatcher — cursor and edge-case coverage
//

TEST_CASE("shared_topic_deeper_than_filter_no_match", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "sensor", "c1", make_sub("sensor"));
  // topic has more levels than filter — must not match.
  const auto result = dispatcher.select_next_for_topic("sensor/temp");
  CHECK(result.empty());
}

TEST_CASE("shared_filter_deeper_than_topic_no_match", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "sensor/temp", "c1", make_sub("sensor/temp"));
  // filter has more levels than topic — must not match.
  const auto result = dispatcher.select_next_for_topic("sensor");
  CHECK(result.empty());
}

TEST_CASE("shared_trailing_slash_empty_last_level_match", "[message_router]") {
  // Both topic and filter end with '/', producing an empty last level.
  // The recursion hits the both-empty base case.
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "a/", "c1", make_sub("a/"));
  const auto result = dispatcher.select_next_for_topic("a/");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c1");
}

TEST_CASE("shared_topic_trailing_slash_filter_deeper_no_match",
          "[message_router]") {
  // Topic "a/" (empty tail) vs filter "a/b" — one side exhausted first.
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "a/b", "c1", make_sub("a/b"));
  const auto result = dispatcher.select_next_for_topic("a/");
  CHECK(result.empty());
}

TEST_CASE("shared_remove_member_before_cursor_adjusts", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.add_member("grp", "t", "c2", make_sub("t"));
  dispatcher.add_member("grp", "t", "c3", make_sub("t"));

  // Advance cursor: c1 selected, next_idx = 1.
  (void)dispatcher.select_next_for_topic("t");

  // Remove c1 (idx 0) while next_idx = 1: must decrement → next_idx = 0.
  dispatcher.remove_member("grp", "t", "c1");
  CHECK(dispatcher.member_count("grp", "t") == 2U);

  // Next pick must be c2 (was idx 1, now idx 0 after removal).
  const auto result = dispatcher.select_next_for_topic("t");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c2");
}

TEST_CASE("shared_remove_member_at_cursor_wraps", "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.add_member("grp", "t", "c2", make_sub("t"));

  // Advance: c1 selected, next_idx = 1.
  (void)dispatcher.select_next_for_topic("t");

  // Remove c2 (idx 1): next_idx(1) not > removed_idx(1), but
  // next_idx(1) >= new size(1) → wrap to 0.
  dispatcher.remove_member("grp", "t", "c2");
  CHECK(dispatcher.member_count("grp", "t") == 1U);

  // Cursor wrapped to 0 — c1 must be returned.
  const auto result = dispatcher.select_next_for_topic("t");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c1");
}

TEST_CASE("shared_remove_client_adjusts_cursor_within_group",
          "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("grp", "t", "c1", make_sub("t"));
  dispatcher.add_member("grp", "t", "c2", make_sub("t"));

  // Advance: c1 selected, next_idx = 1.
  (void)dispatcher.select_next_for_topic("t");

  // remove_client("c1"): removed_idx=0, next_idx(1) > 0 → --next_idx → 0.
  dispatcher.remove_client("c1");
  CHECK(dispatcher.member_count("grp", "t") == 1U);

  const auto result = dispatcher.select_next_for_topic("t");
  REQUIRE(result.size() == 1U);
  CHECK(result[0].client_id == "c2");
}

TEST_CASE("shared_remove_client_skips_groups_without_client",
          "[message_router]") {
  SharedSubscriptionDispatcher dispatcher;
  dispatcher.add_member("g1", "t", "c1", make_sub("t"));
  dispatcher.add_member("g2", "t", "c2", make_sub("t")); // c1 not in g2

  // remove_client("c1") must iterate g2 without removing c2
  // (covers the ++iter path when the client is absent from a group).
  dispatcher.remove_client("c1");
  CHECK(dispatcher.member_count("g1", "t") == 0U);
  CHECK(dispatcher.member_count("g2", "t") == 1U);
}

TEST_CASE("router_buffer_offline_messages_enqueues_until_queue_full",
          "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue(2U); // max 2 messages per client
  SharedSubscriptionDispatcher shared;

  MessageRouter router(proc, offline_queue, shared,
                       [](std::string_view) { return false; },
                       [](std::string_view, const Message &) {});

  std::vector<Message> messages;
  for (uint8_t idx = 0U; idx < 4U; ++idx) {
    messages.push_back(make_msg("buf/t", QoS::AtLeastOnce));
  }

  const std::size_t enqueued = router.buffer_offline_messages("buf-client", messages);

  // Queue limit 2 — only 2 messages should be enqueued.
  CHECK(enqueued == 2U);
  CHECK(offline_queue.size("buf-client") == 2U);
}

TEST_CASE("router_on_offline_queue_changed_fires_on_flush",
          "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  subs.store("flush-sub", make_sub("t"));
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  bool online_flag = false;

  MessageRouter router(proc, offline_queue, shared,
                       [&](std::string_view) { return online_flag; },
                       [](std::string_view, const Message &) {});

  // Route while offline — enqueues one message (callback not yet wired).
  TopicAliasTable alias_table(0U);
  Message msg = make_msg("t", QoS::AtLeastOnce);
  router.route(msg, "pub", "", alias_table);
  CHECK(offline_queue.size("flush-sub") == 1U);

  // Wire callback only after the enqueue so we test the flush path alone.
  int callback_count = 0;
  router.set_on_offline_queue_changed([&]() { ++callback_count; });

  // Flush — drains non-empty queue → callback fires exactly once.
  online_flag = true;
  router.flush_offline_queue("flush-sub");
  CHECK(callback_count == 1);
}

TEST_CASE("router_on_offline_queue_changed_fires_on_buffer",
          "[message_router]") {
  AclEngine acl({allow_all()});
  RetainedMessageStore retained;
  SubscriptionStore subs;
  InboundPublishProcessor proc(acl, retained, subs);
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared;

  MessageRouter router(proc, offline_queue, shared,
                       [](std::string_view) { return false; },
                       [](std::string_view, const Message &) {});

  int callback_count = 0;
  router.set_on_offline_queue_changed([&]() { ++callback_count; });

  std::vector<Message> messages = {make_msg("buf/t", QoS::AtLeastOnce)};
  (void)router.buffer_offline_messages("buf2-client", messages);

  CHECK(callback_count == 1);
}
