#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "authz/acl_engine.h"
#include "authz/acl_rule.h"
#include "data_model/message/message.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/types/variable_byte_integer.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_router.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "store/retained_message_store.h"
#include "store/session_store.h"
#include "store/subscription_store.h"
#include "subscription_manager/subscription_orchestrator.h"

using namespace mqtt;

namespace {

AclRule allow_all_subscribe_rule() {
  return AclRule{.principal = "*",
                 .topic_pattern = "#",
                 .action = AclAction::Subscribe,
                 .effect = AclEffect::Allow};
}

AclRule deny_all_subscribe_rule() {
  return AclRule{.principal = "*",
                 .topic_pattern = "#",
                 .action = AclAction::Subscribe,
                 .effect = AclEffect::Deny};
}

SubscribeOptions make_options(QoS qos_value = QoS::AtMostOnce,
                              bool no_local = false,
                              bool retain_as_published = false,
                              uint8_t retain_handling = 0U) {
  return SubscribeOptions{.max_qos = qos_value,
                          .no_local = no_local,
                          .retain_as_published = retain_as_published,
                          .retain_handling = retain_handling};
}

Message make_retained_message(std::string topic_name, std::vector<uint8_t> payload_data) {
  Message message;
  message.topic = Utf8String{std::move(topic_name)};
  message.payload = BinaryData{std::move(payload_data)};
  message.qos = QoS::AtMostOnce;
  message.retain = true;
  return message;
}

SessionState make_session_state(std::string client_id,
                                uint32_t expiry_interval = 120U) {
  SessionState state;
  state.client_id = Utf8String{std::move(client_id)};
  state.session_expiry_interval = expiry_interval;
  return state;
}

struct Harness {
  explicit Harness(std::vector<AclRule> rules)
      : acl(std::move(rules)), retained(), subscriptions(), inbound(acl, retained, subscriptions),
        offline_queue(), shared_dispatcher(), session_store(), online_clients(), delivered_messages(),
        router(inbound,
               offline_queue,
               shared_dispatcher,
               [this](std::string_view client_id) {
                 return online_clients.contains(std::string(client_id));
               },
               [this](std::string_view client_id, const Message &message) {
                 delivered_messages.emplace_back(std::string(client_id), message);
               }),
        orchestrator(acl, session_store, subscriptions, shared_dispatcher, router) {}

  AclEngine acl;
  RetainedMessageStore retained;
  SubscriptionStore subscriptions;
  InboundPublishProcessor inbound;
  OfflineQueue offline_queue;
  SharedSubscriptionDispatcher shared_dispatcher;
  SessionStore session_store;
  std::unordered_set<std::string> online_clients;
  std::vector<std::pair<std::string, Message>> delivered_messages;
  MessageRouter router;
  SubscriptionOrchestrator orchestrator;
};

SubscribePacket make_subscribe_packet(uint16_t packet_id,
                                      std::string topic_filter,
                                      SubscribeOptions options) {
  SubscribePacket packet;
  packet.packet_id = packet_id;
  packet.filters.push_back(SubscribeFilter{.topic_filter = Utf8String{std::move(topic_filter)},
                                           .options = options});
  return packet;
}

UnsubscribePacket make_unsubscribe_packet(uint16_t packet_id,
                                          std::string topic_filter) {
  UnsubscribePacket packet;
  packet.packet_id = packet_id;
  packet.topic_filters.push_back(Utf8String{std::move(topic_filter)});
  return packet;
}

} // namespace

TEST_CASE("subscribe_regular_filter_stores_subscription_and_grants_qos", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket packet =
      make_subscribe_packet(1U, "sensors/+/temp", make_options(QoS::AtLeastOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);

  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::GrantedQoS1);

  const auto matches = harness.subscriptions.subscribers_for("sensors/kitchen/temp");
  REQUIRE(matches.size() == 1U);
  CHECK(matches[0].client_id == "client-a");
}

TEST_CASE("subscribe_shared_filter_registers_member_only_in_shared_dispatcher", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket packet = make_subscribe_packet(
      2U, "$share/group-a/devices/status", make_options(QoS::AtMostOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);

  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::Success);
  CHECK(harness.shared_dispatcher.member_count("group-a", "devices/status") == 1U);
  CHECK(harness.subscriptions.subscribers_for("devices/status").empty());
}

TEST_CASE("subscribe_invalid_shared_syntax_returns_topic_filter_invalid", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket packet =
      make_subscribe_packet(3U, "$share/group-a", make_options(QoS::AtMostOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);

  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::TopicFilterInvalid);
}

TEST_CASE("subscribe_shared_with_no_local_throws_protocol_error", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket packet = make_subscribe_packet(
      4U, "$share/group-a/devices/status", make_options(QoS::AtMostOnce, true));

  CHECK_THROWS_AS((void)harness.orchestrator.handle_subscribe("client-a", packet),
                  std::runtime_error);
}

TEST_CASE("subscribe_with_identifier_zero_throws_protocol_error", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  SubscribePacket packet =
      make_subscribe_packet(5U, "devices/status", make_options(QoS::AtMostOnce));
  packet.properties.push_back(Property{.id = PropertyId::SubscriptionIdentifier,
                                       .value = VariableByteInteger{0U}});

  CHECK_THROWS_AS((void)harness.orchestrator.handle_subscribe("client-a", packet),
                  std::runtime_error);
}

TEST_CASE("subscribe_denied_by_acl_returns_not_authorized", "[subscription_manager]") {
  Harness harness({deny_all_subscribe_rule()});

  const SubscribePacket packet =
      make_subscribe_packet(6U, "private/topic", make_options(QoS::AtMostOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);

  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::NotAuthorized);
  CHECK(harness.subscriptions.subscribers_for("private/topic").empty());
}

TEST_CASE("subscribe_invalid_topic_filter_returns_topic_filter_invalid", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket packet =
      make_subscribe_packet(7U, "a/#/b", make_options(QoS::AtMostOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);

  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::TopicFilterInvalid);
}

TEST_CASE("subscribe_retain_handling_send_if_new_delivers_only_once", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});
  harness.online_clients.insert("client-a");
  harness.retained.store(make_retained_message("alerts/high", {0x41U}));

  const SubscribePacket packet =
      make_subscribe_packet(8U, "alerts/high", make_options(QoS::AtMostOnce, false, false, 1U));

  const SubackPacket first_suback = harness.orchestrator.handle_subscribe("client-a", packet);
  REQUIRE(first_suback.reason_codes.size() == 1U);
  CHECK(first_suback.reason_codes[0] == ReasonCode::Success);
  REQUIRE(harness.delivered_messages.size() == 1U);
  CHECK(harness.delivered_messages[0].first == "client-a");
  CHECK(harness.delivered_messages[0].second.topic.value == "alerts/high");

  harness.delivered_messages.clear();
  const SubackPacket second_suback = harness.orchestrator.handle_subscribe("client-a", packet);
  REQUIRE(second_suback.reason_codes.size() == 1U);
  CHECK(second_suback.reason_codes[0] == ReasonCode::Success);
  CHECK(harness.delivered_messages.empty());
}

TEST_CASE("subscribe_regular_filter_updates_session_snapshot", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});
  harness.session_store.create(make_session_state("client-a"));

  const SubscribePacket packet =
      make_subscribe_packet(14U, "snapshot/topic", make_options(QoS::AtLeastOnce));

  const SubackPacket suback = harness.orchestrator.handle_subscribe("client-a", packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::GrantedQoS1);

  const auto stored_session = harness.session_store.load("client-a");
  REQUIRE(stored_session.has_value());
  REQUIRE(stored_session->subscriptions.size() == 1U);
  CHECK(stored_session->subscriptions[0].topic_filter.value == "snapshot/topic");
  CHECK(stored_session->subscriptions[0].qos == QoS::AtLeastOnce);
}

TEST_CASE("unsubscribe_regular_filter_returns_success_then_no_subscription_found", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket subscribe_packet =
      make_subscribe_packet(9U, "devices/+/status", make_options(QoS::AtMostOnce));
  (void)harness.orchestrator.handle_subscribe("client-a", subscribe_packet);

  const UnsubscribePacket unsubscribe_packet =
      make_unsubscribe_packet(10U, "devices/+/status");

  const UnsubackPacket first_unsuback =
      harness.orchestrator.handle_unsubscribe("client-a", unsubscribe_packet);
  REQUIRE(first_unsuback.reason_codes.size() == 1U);
  CHECK(first_unsuback.reason_codes[0] == ReasonCode::Success);

  const UnsubackPacket second_unsuback =
      harness.orchestrator.handle_unsubscribe("client-a", unsubscribe_packet);
  REQUIRE(second_unsuback.reason_codes.size() == 1U);
  CHECK(second_unsuback.reason_codes[0] == ReasonCode::NoSubscriptionFound);
}

TEST_CASE("unsubscribe_shared_filter_returns_success_then_no_subscription_found", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const SubscribePacket subscribe_packet =
      make_subscribe_packet(11U, "$share/group-b/devices/status", make_options(QoS::AtMostOnce));
  (void)harness.orchestrator.handle_subscribe("client-a", subscribe_packet);

  const UnsubscribePacket unsubscribe_packet =
      make_unsubscribe_packet(12U, "$share/group-b/devices/status");

  const UnsubackPacket first_unsuback =
      harness.orchestrator.handle_unsubscribe("client-a", unsubscribe_packet);
  REQUIRE(first_unsuback.reason_codes.size() == 1U);
  CHECK(first_unsuback.reason_codes[0] == ReasonCode::Success);

  const UnsubackPacket second_unsuback =
      harness.orchestrator.handle_unsubscribe("client-a", unsubscribe_packet);
  REQUIRE(second_unsuback.reason_codes.size() == 1U);
  CHECK(second_unsuback.reason_codes[0] == ReasonCode::NoSubscriptionFound);
}

TEST_CASE("unsubscribe_invalid_shared_syntax_returns_topic_filter_invalid", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});

  const UnsubscribePacket packet = make_unsubscribe_packet(13U, "$share/group-c");

  const UnsubackPacket unsuback = harness.orchestrator.handle_unsubscribe("client-a", packet);

  REQUIRE(unsuback.reason_codes.size() == 1U);
  CHECK(unsuback.reason_codes[0] == ReasonCode::TopicFilterInvalid);
}

TEST_CASE("unsubscribe_regular_filter_removes_session_snapshot_subscription", "[subscription_manager]") {
  Harness harness({allow_all_subscribe_rule()});
  SessionState session_state = make_session_state("client-a");
  session_state.subscriptions.push_back(
      Subscription{.topic_filter = Utf8String{"devices/+/status"},
                   .qos = QoS::AtMostOnce,
                   .options = SubscriptionOptions{},
                   .identifier = std::nullopt});
  harness.session_store.create(session_state);

  const SubscribePacket subscribe_packet =
      make_subscribe_packet(15U, "devices/+/status", make_options(QoS::AtMostOnce));
  (void)harness.orchestrator.handle_subscribe("client-a", subscribe_packet);

  const UnsubscribePacket unsubscribe_packet =
      make_unsubscribe_packet(16U, "devices/+/status");
  const UnsubackPacket unsuback =
      harness.orchestrator.handle_unsubscribe("client-a", unsubscribe_packet);

  REQUIRE(unsuback.reason_codes.size() == 1U);
  CHECK(unsuback.reason_codes[0] == ReasonCode::Success);

  const auto stored_session = harness.session_store.load("client-a");
  REQUIRE(stored_session.has_value());
  CHECK(stored_session->subscriptions.empty());
}
