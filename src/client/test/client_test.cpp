#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "client/client_error.h"
#include "client/connection_negotiator.h"
#include "client/keep_alive_manager.h"
#include "client/outbound_topic_alias_manager.h"
#include "client/publish_pipeline.h"
#include "client/session_state_keeper.h"
#include "client/subscription_manager.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/property/property_id.h"
#include "data_model/message/message.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_state.h"
#include "data_model/subscription/subscription.h"
#include "data_model/subscription/subscription_options.h"
#include "data_model/types/utf8_string.h"
#include "network/tcp_listener.h"
#include "store/inflight_store.h"

#if !defined(_WIN32)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mqtt {

namespace {

[[nodiscard]] ConnectPacket make_connect_packet() {
  ConnectPacket connect_packet;
  connect_packet.keep_alive = 30U;
  connect_packet.clean_start = true;
  connect_packet.client_id = Utf8String{"client-test"};
  return connect_packet;
}

[[nodiscard]] PublishPacket make_publish_packet(std::string topic_name) {
  PublishPacket publish_packet;
  publish_packet.qos = QoS::AtMostOnce;
  publish_packet.topic = Utf8String{std::move(topic_name)};
  publish_packet.payload = BinaryData{{0x10U, 0x20U}};
  return publish_packet;
}

[[nodiscard]] Message make_message(std::string topic_name, QoS qos,
                                   bool retain = false) {
  Message message;
  message.topic = Utf8String{std::move(topic_name)};
  message.payload = BinaryData{{0x44U, 0x55U}};
  message.qos = qos;
  message.retain = retain;
  return message;
}

[[nodiscard]] Subscription make_subscription(std::string topic_filter, QoS qos) {
  Subscription subscription;
  subscription.topic_filter = Utf8String{std::move(topic_filter)};
  subscription.qos = qos;
  subscription.options = SubscriptionOptions{};
  return subscription;
}

[[nodiscard]] InflightEntry make_inflight_entry(
    uint16_t packet_id, InflightDirection direction,
    std::chrono::steady_clock::time_point timestamp) {
  InflightEntry entry;
  entry.packet_id = packet_id;
  entry.message.topic = Utf8String{"topic/" + std::to_string(packet_id)};
  entry.message.payload = BinaryData{{0x33U}};
  entry.qos = QoS::AtLeastOnce;
  entry.state = InflightState::WaitingForPuback;
  entry.direction = direction;
  entry.timestamp = timestamp;
  return entry;
}

[[nodiscard]] std::optional<uint16_t>
find_topic_alias(const PublishPacket &publish_packet) {
  for (const Property &property : publish_packet.properties) {
    if (property.id != PropertyId::TopicAlias) {
      continue;
    }
    if (const auto *alias_value = std::get_if<TwoByteInteger>(&property.value);
        alias_value != nullptr) {
      return *alias_value;
    }
  }
  return std::nullopt;
}

#if !defined(_WIN32)

void close_fd(int descriptor) {
  if (descriptor >= 0) {
    (void)::close(descriptor);
  }
}

std::array<int, 2> make_socket_pair() {
  std::array<int, 2> descriptors{-1, -1};
  const int create_result =
      ::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.data());
  if (create_result != 0) {
    throw std::runtime_error("socketpair failed");
  }
  return descriptors;
}

#endif

} // namespace

TEST_CASE("keep_alive_disabled_emits_no_action", "[client][keep_alive]") {
  KeepAliveManager manager(0U);
  CHECK_FALSE(manager.is_enabled());
  CHECK(manager.poll() == KeepAliveAction::None);
}

TEST_CASE("keep_alive_emits_pingreq_after_idle_interval", "[client][keep_alive]") {
  KeepAliveManager manager(5U, std::chrono::seconds(2));

  const KeepAliveManager::Clock::time_point start_time =
      KeepAliveManager::Clock::now();
  manager.note_activity(start_time);

  CHECK(manager.poll(start_time + std::chrono::seconds(4)) ==
        KeepAliveAction::None);
  CHECK(manager.poll(start_time + std::chrono::seconds(5)) ==
        KeepAliveAction::SendPingreq);
  CHECK(manager.awaiting_pingresp());
}

TEST_CASE("keep_alive_timeout_when_pingresp_missing", "[client][keep_alive]") {
  KeepAliveManager manager(3U, std::chrono::seconds(1));
  const KeepAliveManager::Clock::time_point start_time =
      KeepAliveManager::Clock::now();

  manager.note_activity(start_time);
  CHECK(manager.poll(start_time + std::chrono::seconds(3)) ==
        KeepAliveAction::SendPingreq);
  CHECK(manager.poll(start_time + std::chrono::seconds(4)) ==
        KeepAliveAction::Timeout);
}

TEST_CASE("keep_alive_pingresp_clears_pending_state", "[client][keep_alive]") {
  KeepAliveManager manager(3U, std::chrono::seconds(2));
  const KeepAliveManager::Clock::time_point start_time =
      KeepAliveManager::Clock::now();

  manager.note_activity(start_time);
  CHECK(manager.poll(start_time + std::chrono::seconds(3)) ==
        KeepAliveAction::SendPingreq);
  manager.on_pingresp(start_time + std::chrono::seconds(4));

  CHECK_FALSE(manager.awaiting_pingresp());
  CHECK(manager.poll(start_time + std::chrono::seconds(5)) ==
        KeepAliveAction::None);
}

TEST_CASE("alias_manager_disabled_does_not_modify_packet", "[client][alias]") {
  OutboundTopicAliasManager manager(0U);
  PublishPacket publish_packet = make_publish_packet("sensor/temp");

  CHECK_FALSE(manager.apply(publish_packet));
  CHECK(publish_packet.topic.value == "sensor/temp");
  CHECK_FALSE(find_topic_alias(publish_packet).has_value());
}

TEST_CASE("alias_manager_first_publish_assigns_alias_and_keeps_topic",
          "[client][alias]") {
  OutboundTopicAliasManager manager(10U);
  PublishPacket publish_packet = make_publish_packet("sensor/temp");

  CHECK(manager.apply(publish_packet));
  CHECK(publish_packet.topic.value == "sensor/temp");
  REQUIRE(find_topic_alias(publish_packet).has_value());
  CHECK(*find_topic_alias(publish_packet) == 1U);
}

TEST_CASE("alias_manager_repeated_publish_reuses_alias_and_clears_topic",
          "[client][alias]") {
  OutboundTopicAliasManager manager(10U);

  PublishPacket first_packet = make_publish_packet("sensor/temp");
  REQUIRE(manager.apply(first_packet));
  const uint16_t first_alias = *find_topic_alias(first_packet);

  PublishPacket second_packet = make_publish_packet("sensor/temp");
  REQUIRE(manager.apply(second_packet));
  REQUIRE(find_topic_alias(second_packet).has_value());

  CHECK(*find_topic_alias(second_packet) == first_alias);
  CHECK(second_packet.topic.value.empty());
}

TEST_CASE("alias_manager_rejects_empty_topic", "[client][alias]") {
  OutboundTopicAliasManager manager(5U);
  PublishPacket publish_packet = make_publish_packet("");

  CHECK_THROWS_AS(manager.apply(publish_packet), ClientException);
}

TEST_CASE("alias_manager_respects_reset_and_reports_maximum", "[client][alias]") {
  OutboundTopicAliasManager manager(3U);
  CHECK(manager.max_aliases() == 3U);

  PublishPacket first_packet = make_publish_packet("topic/one");
  REQUIRE(manager.apply(first_packet));
  REQUIRE(find_topic_alias(first_packet).has_value());
  CHECK(*find_topic_alias(first_packet) == 1U);

  manager.reset();

  PublishPacket second_packet = make_publish_packet("topic/two");
  REQUIRE(manager.apply(second_packet));
  REQUIRE(find_topic_alias(second_packet).has_value());
  CHECK(*find_topic_alias(second_packet) == 1U);
}

TEST_CASE("alias_manager_updates_existing_topic_alias_property", "[client][alias]") {
  OutboundTopicAliasManager manager(3U);

  PublishPacket first_packet = make_publish_packet("topic/name");
  REQUIRE(manager.apply(first_packet));

  PublishPacket second_packet = make_publish_packet("topic/name");
  second_packet.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = TwoByteInteger{99U}});

  REQUIRE(manager.apply(second_packet));
  REQUIRE(find_topic_alias(second_packet).has_value());
  CHECK(*find_topic_alias(second_packet) == 1U);
  CHECK(second_packet.topic.value.empty());
  CHECK(second_packet.properties.size() == 1U);
}

TEST_CASE("alias_manager_reuses_alias_when_capacity_is_full", "[client][alias]") {
  OutboundTopicAliasManager manager(1U);

  PublishPacket first_topic_packet = make_publish_packet("topic/one");
  REQUIRE(manager.apply(first_topic_packet));
  REQUIRE(find_topic_alias(first_topic_packet).has_value());
  CHECK(*find_topic_alias(first_topic_packet) == 1U);

  PublishPacket second_topic_packet = make_publish_packet("topic/two");
  REQUIRE(manager.apply(second_topic_packet));
  REQUIRE(find_topic_alias(second_topic_packet).has_value());
  CHECK(*find_topic_alias(second_topic_packet) == 1U);

  PublishPacket first_topic_again_packet = make_publish_packet("topic/one");
  REQUIRE(manager.apply(first_topic_again_packet));
  REQUIRE(find_topic_alias(first_topic_again_packet).has_value());
  CHECK(*find_topic_alias(first_topic_again_packet) == 1U);
  CHECK(first_topic_again_packet.topic.value == "topic/one");
}

TEST_CASE("connection_negotiator_dial_tcp_invalid_host_throws",
          "[client][negotiator]") {
  CHECK_THROWS_AS(
      ConnectionNegotiator::dial_tcp("definitely.invalid.host.name", 1883U),
      ClientException);
}

TEST_CASE("connection_negotiator_write_failure_throws", "[client][negotiator]") {
  TcpConnection invalid_connection(k_invalid_socket);

  try {
    (void)ConnectionNegotiator::negotiate(invalid_connection, make_connect_packet(),
                                          50U);
    FAIL("expected write failure exception");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::WriteFailed);
  }
}

TEST_CASE("session_state_keeper_upsert_and_remove_subscriptions",
          "[client][state_keeper]") {
  ClientSessionStateKeeper keeper("client-state", 120U);

  keeper.upsert_subscription(make_subscription("sensors/one", QoS::AtMostOnce));
  keeper.upsert_subscription(make_subscription("sensors/two", QoS::AtLeastOnce));
  keeper.upsert_subscription(make_subscription("sensors/one", QoS::ExactlyOnce));

  REQUIRE(keeper.subscriptions().size() == 2U);
  CHECK(keeper.subscriptions().front().topic_filter.value == "sensors/one");
  CHECK(keeper.subscriptions().front().qos == QoS::ExactlyOnce);

  CHECK(keeper.remove_subscription("sensors/two"));
  CHECK_FALSE(keeper.remove_subscription("sensors/missing"));
  keeper.clear_subscriptions();
  CHECK(keeper.subscriptions().empty());
}

TEST_CASE("session_state_keeper_restore_plan_respects_clean_start",
          "[client][state_keeper]") {
  ClientSessionStateKeeper keeper("client-state", 777U);
  keeper.upsert_subscription(make_subscription("restore/filter", QoS::AtLeastOnce));

  const auto now = std::chrono::steady_clock::now();
  keeper.set_outbound_inflight({
      make_inflight_entry(9U, InflightDirection::Outbound, now),
  });

  const ClientSessionRestorePlan clean_plan = keeper.build_restore_plan(true);
  CHECK(clean_plan.subscriptions.empty());
  CHECK(clean_plan.outbound_inflight.empty());
  CHECK(clean_plan.session_expiry_interval == 0U);

  const ClientSessionRestorePlan resume_plan = keeper.build_restore_plan(false);
  REQUIRE(resume_plan.subscriptions.size() == 1U);
  REQUIRE(resume_plan.outbound_inflight.size() == 1U);
  CHECK(resume_plan.session_expiry_interval == 777U);
}

TEST_CASE("session_state_keeper_set_outbound_inflight_filters_and_sorts",
          "[client][state_keeper]") {
  ClientSessionStateKeeper keeper("client-state", 10U);
  const auto now = std::chrono::steady_clock::now();

  keeper.set_outbound_inflight({
      make_inflight_entry(3U, InflightDirection::Outbound, now + std::chrono::seconds(3)),
      make_inflight_entry(0U, InflightDirection::Outbound, now + std::chrono::seconds(1)),
      make_inflight_entry(2U, InflightDirection::Inbound, now + std::chrono::seconds(2)),
      make_inflight_entry(1U, InflightDirection::Outbound, now + std::chrono::seconds(1)),
  });

  REQUIRE(keeper.outbound_inflight().size() == 2U);
  CHECK(keeper.outbound_inflight()[0].packet_id == 1U);
  CHECK(keeper.outbound_inflight()[1].packet_id == 3U);
}

TEST_CASE("session_state_keeper_capture_outbound_inflight_from_store",
          "[client][state_keeper]") {
  InflightStore inflight_store;
  const auto now = std::chrono::steady_clock::now();

  inflight_store.create("client-state",
                        make_inflight_entry(11U, InflightDirection::Outbound,
                                            now + std::chrono::seconds(2)));
  inflight_store.create("client-state",
                        make_inflight_entry(12U, InflightDirection::Inbound,
                                            now + std::chrono::seconds(1)));
  inflight_store.create("other-client",
                        make_inflight_entry(13U, InflightDirection::Outbound,
                                            now + std::chrono::seconds(3)));

  ClientSessionStateKeeper keeper("client-state", 0U);
  keeper.capture_outbound_inflight(inflight_store);

  REQUIRE(keeper.outbound_inflight().size() == 1U);
  CHECK(keeper.outbound_inflight().front().packet_id == 11U);
}

TEST_CASE("session_state_keeper_snapshot_roundtrip_and_mismatch_guard",
          "[client][state_keeper]") {
  ClientSessionStateKeeper keeper("client-state", 400U);
  keeper.upsert_subscription(make_subscription("alpha", QoS::AtLeastOnce));
  keeper.set_outbound_inflight(
      {make_inflight_entry(21U, InflightDirection::Outbound,
                           std::chrono::steady_clock::now())});

  const ClientSessionSnapshot state_snapshot = keeper.snapshot();

  ClientSessionStateKeeper restored_keeper("client-state", 0U);
  restored_keeper.apply_snapshot(state_snapshot);
  REQUIRE(restored_keeper.subscriptions().size() == 1U);
  REQUIRE(restored_keeper.outbound_inflight().size() == 1U);
  CHECK(restored_keeper.session_expiry_interval() == 400U);

  ClientSessionSnapshot mismatched_snapshot = state_snapshot;
  mismatched_snapshot.session_state.client_id = Utf8String{"other-client"};

  CHECK_THROWS_AS(restored_keeper.apply_snapshot(mismatched_snapshot),
                  ClientException);
}

  TEST_CASE(
    "subscription_manager_begin_subscribe_builds_packet_and_activates_on_suback",
    "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");
    std::size_t callback_invocations = 0U;

    ClientSubscriptionManager::SubscribeRequest subscribe_request;
    subscribe_request.topic_filter = "sensors/room1";
    subscribe_request.requested_qos = QoS::AtLeastOnce;
    subscribe_request.callback =
      [&callback_invocations](const PublishPacket &) { ++callback_invocations; };

    const ClientSubscriptionManager::SubscribeOperation subscribe_operation =
      manager.begin_subscribe({subscribe_request});
    REQUIRE(subscribe_operation.packet_id != 0U);
    REQUIRE(subscribe_operation.packet.packet_id == subscribe_operation.packet_id);
    REQUIRE(subscribe_operation.packet.filters.size() == 1U);

    SubackPacket suback_packet;
    suback_packet.packet_id = subscribe_operation.packet_id;
    suback_packet.reason_codes = {ReasonCode::GrantedQoS1};
    const ClientSubscriptionManager::AckResult suback_result =
      manager.on_suback(suback_packet);
    REQUIRE(suback_result.reason_codes.size() == 1U);
    CHECK(suback_result.reason_codes.front() == ReasonCode::GrantedQoS1);
    CHECK(manager.has_subscription("sensors/room1"));

    const PublishPacket inbound_publish = make_publish_packet("sensors/room1");
    CHECK(manager.dispatch_inbound_publish(inbound_publish) == 1U);
    CHECK(callback_invocations == 1U);
  }

  TEST_CASE("subscription_manager_suback_reject_keeps_filter_inactive",
        "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");
    std::size_t callback_invocations = 0U;

    ClientSubscriptionManager::SubscribeRequest subscribe_request;
    subscribe_request.topic_filter = "sensors/rejected";
    subscribe_request.requested_qos = QoS::AtLeastOnce;
    subscribe_request.callback =
      [&callback_invocations](const PublishPacket &) { ++callback_invocations; };

    const ClientSubscriptionManager::SubscribeOperation subscribe_operation =
      manager.begin_subscribe({subscribe_request});

    SubackPacket suback_packet;
    suback_packet.packet_id = subscribe_operation.packet_id;
    suback_packet.reason_codes = {ReasonCode::NotAuthorized};
    (void)manager.on_suback(suback_packet);

    CHECK_FALSE(manager.has_subscription("sensors/rejected"));
    CHECK(manager.dispatch_inbound_publish(make_publish_packet("sensors/rejected")) ==
      0U);
    CHECK(callback_invocations == 0U);
  }

  TEST_CASE("subscription_manager_begin_unsubscribe_and_unsuback_remove_filter",
        "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");
    std::size_t callback_invocations = 0U;

    ClientSubscriptionManager::SubscribeRequest subscribe_request;
    subscribe_request.topic_filter = "devices/+/status";
    subscribe_request.requested_qos = QoS::AtMostOnce;
    subscribe_request.callback =
      [&callback_invocations](const PublishPacket &) { ++callback_invocations; };

    const ClientSubscriptionManager::SubscribeOperation subscribe_operation =
      manager.begin_subscribe({subscribe_request});
      const ClientSubscriptionManager::AckResult subscribe_ack_result =
        manager.on_suback(SubackPacket{.packet_id = subscribe_operation.packet_id,
                       .properties = {},
                       .reason_codes = {ReasonCode::Success}});
      CHECK(subscribe_ack_result.packet_id == subscribe_operation.packet_id);

    CHECK(manager.has_subscription("devices/+/status"));
    CHECK(manager.dispatch_inbound_publish(make_publish_packet("devices/a/status")) ==
      1U);
    CHECK(callback_invocations == 1U);

    const ClientSubscriptionManager::UnsubscribeOperation unsubscribe_operation =
      manager.begin_unsubscribe({"devices/+/status"});
      const ClientSubscriptionManager::AckResult unsubscribe_ack_result =
        manager.on_unsuback(UnsubackPacket{.packet_id = unsubscribe_operation.packet_id,
                         .properties = {},
                         .reason_codes = {ReasonCode::Success}});
      CHECK(unsubscribe_ack_result.packet_id == unsubscribe_operation.packet_id);

    CHECK_FALSE(manager.has_subscription("devices/+/status"));
    CHECK(manager.dispatch_inbound_publish(make_publish_packet("devices/b/status")) ==
      0U);
    CHECK(callback_invocations == 1U);
  }

  TEST_CASE("subscription_manager_suback_unknown_packet_id_throws",
        "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");

    SubackPacket suback_packet;
    suback_packet.packet_id = 65535U;
    suback_packet.reason_codes = {ReasonCode::Success};
    CHECK_THROWS_AS(manager.on_suback(suback_packet), ClientException);
  }

  TEST_CASE("subscription_manager_reason_count_mismatch_throws",
        "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");

    ClientSubscriptionManager::SubscribeRequest first_request;
    first_request.topic_filter = "devices/one";
    first_request.callback = [](const PublishPacket &) {};

    ClientSubscriptionManager::SubscribeRequest second_request;
    second_request.topic_filter = "devices/two";
    second_request.callback = [](const PublishPacket &) {};

    const ClientSubscriptionManager::SubscribeOperation subscribe_operation =
      manager.begin_subscribe({first_request, second_request});

    SubackPacket suback_packet;
    suback_packet.packet_id = subscribe_operation.packet_id;
    suback_packet.reason_codes = {ReasonCode::Success};
    CHECK_THROWS_AS(manager.on_suback(suback_packet), ClientException);
  }

  TEST_CASE("subscription_manager_validates_filters_and_topic_name",
        "[client][subscription]") {
    ClientSubscriptionManager manager("client-subscription");

    ClientSubscriptionManager::SubscribeRequest invalid_request;
    invalid_request.topic_filter = "invalid#filter";
    invalid_request.callback = [](const PublishPacket &) {};
    CHECK_THROWS_AS(manager.begin_subscribe({invalid_request}), ClientException);
    CHECK_THROWS_AS(manager.begin_unsubscribe({"invalid#filter"}),
            ClientException);

    PublishPacket invalid_publish = make_publish_packet("topic/#");
    CHECK_THROWS_AS(manager.dispatch_inbound_publish(invalid_publish),
            ClientException);
  }

    TEST_CASE("publish_pipeline_qos0_completes_immediately", "[client][publish]") {
      ClientPublishPipeline pipeline;

      const Message outbound_message = make_message("publish/qos0", QoS::AtMostOnce);
      const ClientPublishPipeline::PublishStartResult start_result =
        pipeline.begin_publish(outbound_message);

      CHECK(start_result.completed);
      CHECK_FALSE(start_result.packet_id.has_value());
      CHECK(start_result.publish_packet.topic.value == "publish/qos0");
      CHECK(start_result.publish_packet.qos == QoS::AtMostOnce);
      CHECK(pipeline.pending_count() == 0U);

      const WriteBuffer publish_frame =
        ClientPublishPipeline::encode_publish_frame(start_result.publish_packet);
      CHECK_FALSE(publish_frame.empty());
    }

    TEST_CASE("publish_pipeline_qos1_assigns_packet_id_and_completes_on_puback",
          "[client][publish]") {
      ClientPublishPipeline pipeline;

      const ClientPublishPipeline::PublishStartResult start_result =
        pipeline.begin_publish(make_message("publish/qos1", QoS::AtLeastOnce));
      REQUIRE_FALSE(start_result.completed);
      REQUIRE(start_result.packet_id.has_value());
      CHECK(pipeline.has_pending(*start_result.packet_id));

      const WriteBuffer publish_frame =
        ClientPublishPipeline::encode_publish_frame(start_result.publish_packet);
      CHECK_FALSE(publish_frame.empty());

      PubackPacket puback_packet;
      puback_packet.packet_id = *start_result.packet_id;
      puback_packet.reason_code = ReasonCode::Success;

      const ClientPublishPipeline::PublishAckResult ack_result =
        pipeline.on_puback(puback_packet);
      CHECK(ack_result.completed);
      CHECK_FALSE(ack_result.send_pubrel);
      CHECK_FALSE(ack_result.pubrel_packet.has_value());
      CHECK(ack_result.reason_code == ReasonCode::Success);
      CHECK_FALSE(pipeline.has_pending(*start_result.packet_id));
    }

    TEST_CASE(
      "publish_pipeline_qos2_pubrec_success_emits_pubrel_then_pubcomp_completes",
      "[client][publish]") {
      ClientPublishPipeline pipeline;

      const ClientPublishPipeline::PublishStartResult start_result =
        pipeline.begin_publish(make_message("publish/qos2", QoS::ExactlyOnce));
      REQUIRE(start_result.packet_id.has_value());

      PubrecPacket pubrec_packet;
      pubrec_packet.packet_id = *start_result.packet_id;
      pubrec_packet.reason_code = ReasonCode::Success;

      const ClientPublishPipeline::PublishAckResult pubrec_result =
        pipeline.on_pubrec(pubrec_packet);
      CHECK_FALSE(pubrec_result.completed);
      CHECK(pubrec_result.send_pubrel);
      REQUIRE(pubrec_result.pubrel_packet.has_value());
      CHECK(pubrec_result.pubrel_packet->packet_id == *start_result.packet_id);
      CHECK(pipeline.has_pending(*start_result.packet_id));

      const WriteBuffer pubrel_frame =
        ClientPublishPipeline::encode_pubrel_frame(*pubrec_result.pubrel_packet);
      CHECK_FALSE(pubrel_frame.empty());

      PubcompPacket pubcomp_packet;
      pubcomp_packet.packet_id = *start_result.packet_id;
      pubcomp_packet.reason_code = ReasonCode::Success;

      const ClientPublishPipeline::PublishAckResult pubcomp_result =
        pipeline.on_pubcomp(pubcomp_packet);
      CHECK(pubcomp_result.completed);
      CHECK_FALSE(pubcomp_result.send_pubrel);
      CHECK_FALSE(pipeline.has_pending(*start_result.packet_id));
    }

    TEST_CASE("publish_pipeline_qos2_pubrec_error_completes_without_pubrel",
          "[client][publish]") {
      ClientPublishPipeline pipeline;

      const ClientPublishPipeline::PublishStartResult start_result =
        pipeline.begin_publish(make_message("publish/qos2/error", QoS::ExactlyOnce));
      REQUIRE(start_result.packet_id.has_value());

      PubrecPacket pubrec_packet;
      pubrec_packet.packet_id = *start_result.packet_id;
      pubrec_packet.reason_code = ReasonCode::NotAuthorized;

      const ClientPublishPipeline::PublishAckResult ack_result =
        pipeline.on_pubrec(pubrec_packet);
      CHECK(ack_result.completed);
      CHECK_FALSE(ack_result.send_pubrel);
      CHECK_FALSE(ack_result.pubrel_packet.has_value());
      CHECK(ack_result.reason_code == ReasonCode::NotAuthorized);
      CHECK_FALSE(pipeline.has_pending(*start_result.packet_id));
    }

    TEST_CASE("publish_pipeline_unknown_packet_id_and_wrong_stage_throws",
          "[client][publish]") {
      ClientPublishPipeline pipeline;

      CHECK_THROWS_AS(pipeline.on_puback(PubackPacket{.packet_id = 1234U}),
              ClientException);

      const ClientPublishPipeline::PublishStartResult start_result =
        pipeline.begin_publish(make_message("publish/wrong-stage", QoS::ExactlyOnce));
      REQUIRE(start_result.packet_id.has_value());

      CHECK_THROWS_AS(
        pipeline.on_puback(PubackPacket{.packet_id = *start_result.packet_id}),
        ClientException);
    }

    TEST_CASE("publish_pipeline_rejects_invalid_topic", "[client][publish]") {
      ClientPublishPipeline pipeline;
      CHECK_THROWS_AS(
        pipeline.begin_publish(make_message("invalid/#/topic", QoS::AtLeastOnce)),
        ClientException);
    }

#if !defined(_WIN32)

TEST_CASE("connection_negotiator_successfully_parses_connack",
          "[client][negotiator]") {
  std::array<int, 2> descriptors = make_socket_pair();
  const int client_descriptor = descriptors[0];
  const int server_descriptor = descriptors[1];

  std::thread server_thread([server_descriptor]() {
    std::array<uint8_t, 1024> read_buffer{};
    (void)::recv(server_descriptor, read_buffer.data(), read_buffer.size(), 0);

    ConnackPacket connack_packet;
    connack_packet.session_present = true;
    connack_packet.reason_code = ReasonCode::Success;
    connack_packet.properties = {
        Property{.id = PropertyId::ReceiveMaximum, .value = TwoByteInteger{25U}},
        Property{.id = PropertyId::TopicAliasMaximum,
                 .value = TwoByteInteger{9U}},
        Property{.id = PropertyId::ServerKeepAlive, .value = TwoByteInteger{15U}},
        Property{.id = PropertyId::AssignedClientIdentifier,
                 .value = Utf8String{"assigned-id"}},
    };

    WriteBuffer connack_frame;
    encode_connack(connack_frame, connack_packet);
    (void)::send(server_descriptor, connack_frame.data(), connack_frame.size(), 0);
    close_fd(server_descriptor);
  });

  TcpConnection client_connection(static_cast<SocketHandle>(client_descriptor));
  const ConnectionNegotiationResult result = ConnectionNegotiator::negotiate(
      client_connection, make_connect_packet(), 2000U);

  CHECK(result.session_present);
  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.receive_maximum == 25U);
  CHECK(result.topic_alias_maximum == 9U);
  REQUIRE(result.server_keep_alive.has_value());
  CHECK(*result.server_keep_alive == 15U);
  REQUIRE(result.assigned_client_id.has_value());
  CHECK(*result.assigned_client_id == "assigned-id");

  server_thread.join();
}

TEST_CASE("connection_negotiator_dial_tcp_local_listener_success",
          "[client][negotiator]") {
  TcpListener listener = TcpListener::listen(0U);
  const uint16_t listen_port = listener.port();
  REQUIRE(listen_port != 0U);

  std::thread accept_thread([&listener]() {
    std::unique_ptr<TcpConnection> accepted_connection = listener.accept();
    if (accepted_connection != nullptr) {
      accepted_connection->close();
    }
  });

  TcpConnection client_connection =
      ConnectionNegotiator::dial_tcp("127.0.0.1", listen_port);
  CHECK(client_connection.is_open());
  client_connection.close();

  accept_thread.join();
  listener.close();
}

TEST_CASE("connection_negotiator_dial_tcp_connect_failure_throws",
          "[client][negotiator]") {
  const uint16_t unused_port = 1U;

  try {
    (void)ConnectionNegotiator::dial_tcp("127.0.0.1", unused_port);
    FAIL("expected connect failure exception");
  } catch (const ClientException &exception) {
    const bool is_expected_error =
        exception.error() == ClientError::ConnectFailed ||
        exception.error() == ClientError::ResolveFailed;
    CHECK(is_expected_error);
  }
}

TEST_CASE("connection_negotiator_rejected_connack_throws",
          "[client][negotiator]") {
  std::array<int, 2> descriptors = make_socket_pair();
  const int client_descriptor = descriptors[0];
  const int server_descriptor = descriptors[1];

  std::thread server_thread([server_descriptor]() {
    std::array<uint8_t, 512> read_buffer{};
    (void)::recv(server_descriptor, read_buffer.data(), read_buffer.size(), 0);

    ConnackPacket connack_packet;
    connack_packet.reason_code = ReasonCode::BadUserNameOrPassword;

    WriteBuffer connack_frame;
    encode_connack(connack_frame, connack_packet);
    (void)::send(server_descriptor, connack_frame.data(), connack_frame.size(), 0);
    close_fd(server_descriptor);
  });

  TcpConnection client_connection(static_cast<SocketHandle>(client_descriptor));

  try {
    (void)ConnectionNegotiator::negotiate(client_connection, make_connect_packet(),
                                          2000U);
    FAIL("expected negotiation rejection exception");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::NegotiationRejected);
    REQUIRE(exception.reason_code().has_value());
    CHECK(*exception.reason_code() == ReasonCode::BadUserNameOrPassword);
  }

  server_thread.join();
}

TEST_CASE("connection_negotiator_non_connack_response_throws_protocol_error",
          "[client][negotiator]") {
  std::array<int, 2> descriptors = make_socket_pair();
  const int client_descriptor = descriptors[0];
  const int server_descriptor = descriptors[1];

  std::thread server_thread([server_descriptor]() {
    std::array<uint8_t, 512> read_buffer{};
    (void)::recv(server_descriptor, read_buffer.data(), read_buffer.size(), 0);

    WriteBuffer pingresp_frame;
    encode_pingresp(pingresp_frame);
    (void)::send(server_descriptor, pingresp_frame.data(), pingresp_frame.size(), 0);
    close_fd(server_descriptor);
  });

  TcpConnection client_connection(static_cast<SocketHandle>(client_descriptor));

  try {
    (void)ConnectionNegotiator::negotiate(client_connection, make_connect_packet(),
                                          2000U);
    FAIL("expected protocol error exception");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::ProtocolError);
  }

  server_thread.join();
}

TEST_CASE("connection_negotiator_peer_close_before_connack_throws",
          "[client][negotiator]") {
  std::array<int, 2> descriptors = make_socket_pair();
  const int client_descriptor = descriptors[0];
  const int server_descriptor = descriptors[1];

  std::thread server_thread([server_descriptor]() {
    std::array<uint8_t, 512> read_buffer{};
    (void)::recv(server_descriptor, read_buffer.data(), read_buffer.size(), 0);
    close_fd(server_descriptor);
  });

  TcpConnection client_connection(static_cast<SocketHandle>(client_descriptor));

  try {
    (void)ConnectionNegotiator::negotiate(client_connection, make_connect_packet(),
                                          2000U);
    FAIL("expected read failure exception");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::ReadFailed);
  }

  server_thread.join();
}

TEST_CASE("connection_negotiator_timeout_waiting_for_connack_throws",
          "[client][negotiator]") {
  std::array<int, 2> descriptors = make_socket_pair();
  const int client_descriptor = descriptors[0];
  const int server_descriptor = descriptors[1];

  std::thread server_thread([server_descriptor]() {
    std::array<uint8_t, 512> read_buffer{};
    (void)::recv(server_descriptor, read_buffer.data(), read_buffer.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    close_fd(server_descriptor);
  });

  TcpConnection client_connection(static_cast<SocketHandle>(client_descriptor));

  try {
    (void)ConnectionNegotiator::negotiate(client_connection, make_connect_packet(),
                                          50U);
    FAIL("expected timeout exception");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::Timeout);
  }

  server_thread.join();
}

#endif

} // namespace mqtt
