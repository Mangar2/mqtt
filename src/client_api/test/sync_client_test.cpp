#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "client_api/sync_client.h"

namespace mqtt {

namespace {

[[nodiscard]] ConnectPacket make_connect_packet() {
  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"sync-client"};
  connect_packet.clean_start = true;
  connect_packet.keep_alive = 30U;
  return connect_packet;
}

[[nodiscard]] Message make_message(std::string topic, QoS qos) {
  Message message;
  message.topic = Utf8String{std::move(topic)};
  message.qos = qos;
  message.payload = BinaryData{{0x41U}};
  return message;
}

[[nodiscard]] ConnectionNegotiationResult make_negotiation_result() {
  ConnectionNegotiationResult negotiation_result;
  negotiation_result.session_present = false;
  negotiation_result.reason_code = ReasonCode::Success;
  return negotiation_result;
}

} // namespace

TEST_CASE("sync_client_connect_uses_callback_and_marks_connected",
          "[client_api][sync]") {
  SyncClient client("sync-client");

  uint32_t observed_timeout_ms = 0U;
  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate =
      [&observed_timeout_ms](const ConnectPacket &, uint32_t timeout_ms) {
        observed_timeout_ms = timeout_ms;
        return make_negotiation_result();
      };
  client.set_callbacks(std::move(callbacks));

  const ConnectionNegotiationResult negotiation_result =
      client.connect(make_connect_packet(), 4321U);
  CHECK(negotiation_result.reason_code == ReasonCode::Success);
  CHECK(observed_timeout_ms == 4321U);
  CHECK(client.is_connected());
}

TEST_CASE("sync_client_publish_qos0_sends_once_and_returns_success",
          "[client_api][sync]") {
  SyncClient client("sync-client");
  std::size_t sent_publish_count = 0U;

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_publish = [&sent_publish_count](const PublishPacket &) {
    ++sent_publish_count;
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  const ReasonCode reason_code =
      client.publish(make_message("topic/qos0", QoS::AtMostOnce), 999U);
  CHECK(reason_code == ReasonCode::Success);
  CHECK(sent_publish_count == 1U);
}

TEST_CASE("sync_client_publish_qos2_runs_pubrec_pubrel_pubcomp_sequence",
          "[client_api][sync]") {
  SyncClient client("sync-client");
  std::size_t pubrel_send_count = 0U;

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_publish = [](const PublishPacket &) {};
  callbacks.wait_pubrec = [](uint16_t packet_id, uint32_t) {
    return PubrecPacket{.packet_id = packet_id,
                        .reason_code = ReasonCode::Success,
                        .properties = {}};
  };
  callbacks.send_pubrel = [&pubrel_send_count](const PubrelPacket &) {
    ++pubrel_send_count;
  };
  callbacks.wait_pubcomp = [](uint16_t packet_id, uint32_t) {
    return PubcompPacket{.packet_id = packet_id,
                         .reason_code = ReasonCode::Success,
                         .properties = {}};
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  const ReasonCode reason_code =
      client.publish(make_message("topic/qos2", QoS::ExactlyOnce), 3000U);
  CHECK(reason_code == ReasonCode::Success);
  CHECK(pubrel_send_count == 1U);
}

TEST_CASE(
    "sync_client_subscribe_and_unsubscribe_roundtrip_updates_active_filters",
    "[client_api][sync]") {
  SyncClient client("sync-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_id, uint32_t) {
    return SubackPacket{.packet_id = packet_id,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  callbacks.send_unsubscribe = [](const UnsubscribePacket &) {};
  callbacks.wait_unsuback = [](uint16_t packet_id, uint32_t) {
    return UnsubackPacket{.packet_id = packet_id,
                          .properties = {},
                          .reason_codes = {ReasonCode::Success}};
  };

  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  ClientSubscriptionManager::SubscribeRequest subscribe_request;
  subscribe_request.topic_filter = "sensor/+/temp";
  subscribe_request.requested_qos = QoS::AtLeastOnce;
  subscribe_request.callback = [](const PublishPacket &) {};

  const std::vector<ReasonCode> subscribe_reasons =
      client.subscribe({subscribe_request}, 2000U);
  REQUIRE(subscribe_reasons.size() == 1U);
  CHECK(subscribe_reasons.front() == ReasonCode::GrantedQoS1);
  CHECK(client.has_subscription("sensor/+/temp"));

  const std::vector<ReasonCode> unsubscribe_reasons =
      client.unsubscribe({"sensor/+/temp"}, 2000U);
  REQUIRE(unsubscribe_reasons.size() == 1U);
  CHECK(unsubscribe_reasons.front() == ReasonCode::Success);
  CHECK_FALSE(client.has_subscription("sensor/+/temp"));
}

TEST_CASE("sync_client_publish_qos1_requires_puback_callback",
          "[client_api][sync]") {
  SyncClient client("sync-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_publish = [](const PublishPacket &) {};
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  try {
    (void)client.publish(make_message("topic/qos1", QoS::AtLeastOnce), 1000U);
    FAIL("expected ClientException");
  } catch (const ClientException &exception) {
    CHECK(exception.error() == ClientError::Timeout);
  }
}

TEST_CASE("sync_client_operations_require_connected_state",
          "[client_api][sync]") {
  SyncClient client("sync-client");

  CHECK_THROWS_AS(client.publish(make_message("topic", QoS::AtMostOnce)),
                  ClientException);

  ClientSubscriptionManager::SubscribeRequest subscribe_request;
  subscribe_request.topic_filter = "sensor/#";
  subscribe_request.requested_qos = QoS::AtMostOnce;
  subscribe_request.callback = [](const PublishPacket &) {};

  CHECK_THROWS_AS(client.subscribe({subscribe_request}), ClientException);
  CHECK_THROWS_AS(client.unsubscribe({"sensor/#"}), ClientException);
}

TEST_CASE("sync_client_rejects_empty_client_id", "[client_api][sync]") {
  CHECK_THROWS_AS(SyncClient(""), ClientException);
}

TEST_CASE("sync_client_connect_requires_connect_callback", "[client_api][sync]") {
  SyncClient client("sync-client");
  CHECK_THROWS_AS(client.connect(make_connect_packet(), 100U), ClientException);
}

TEST_CASE("sync_client_publish_qos2_error_pubrec_finishes_without_pubrel",
          "[client_api][sync]") {
  SyncClient client("sync-client");
  std::size_t pubrel_send_count = 0U;
  std::size_t pubcomp_wait_count = 0U;

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_publish = [](const PublishPacket &) {};
  callbacks.wait_pubrec = [](uint16_t packet_id, uint32_t) {
    return PubrecPacket{.packet_id = packet_id,
                        .reason_code = ReasonCode::NotAuthorized,
                        .properties = {}};
  };
  callbacks.send_pubrel = [&pubrel_send_count](const PubrelPacket &) {
    ++pubrel_send_count;
  };
  callbacks.wait_pubcomp = [&pubcomp_wait_count](uint16_t packet_id, uint32_t) {
    ++pubcomp_wait_count;
    return PubcompPacket{.packet_id = packet_id,
                         .reason_code = ReasonCode::Success,
                         .properties = {}};
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  const ReasonCode reason_code =
      client.publish(make_message("topic/qos2/error", QoS::ExactlyOnce), 1000U);
  CHECK(reason_code == ReasonCode::NotAuthorized);
  CHECK(pubrel_send_count == 0U);
  CHECK(pubcomp_wait_count == 0U);
}

TEST_CASE(
    "sync_client_subscribe_unsubscribe_rejected_reasons_keep_state_stable",
    "[client_api][sync]") {
  SyncClient client("sync-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_id, uint32_t) {
    return SubackPacket{.packet_id = packet_id,
                        .properties = {},
                        .reason_codes = {ReasonCode::NotAuthorized}};
  };
  callbacks.send_unsubscribe = [](const UnsubscribePacket &) {};
  callbacks.wait_unsuback = [](uint16_t packet_id, uint32_t) {
    return UnsubackPacket{.packet_id = packet_id,
                          .properties = {},
                          .reason_codes = {ReasonCode::NotAuthorized}};
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  ClientSubscriptionManager::SubscribeRequest subscribe_request;
  subscribe_request.topic_filter = "sensor/rejected";
  subscribe_request.requested_qos = QoS::AtLeastOnce;
  subscribe_request.callback = [](const PublishPacket &) {};

  const std::vector<ReasonCode> subscribe_reasons =
      client.subscribe({subscribe_request}, 500U);
  REQUIRE(subscribe_reasons.size() == 1U);
  CHECK(subscribe_reasons.front() == ReasonCode::NotAuthorized);
  CHECK_FALSE(client.has_subscription("sensor/rejected"));

  const std::vector<ReasonCode> unsubscribe_reasons =
      client.unsubscribe({"sensor/rejected"}, 500U);
  REQUIRE(unsubscribe_reasons.size() == 1U);
  CHECK(unsubscribe_reasons.front() == ReasonCode::NotAuthorized);
  CHECK_FALSE(client.has_subscription("sensor/rejected"));
}

TEST_CASE(
    "sync_client_disconnect_without_callback_and_already_disconnected_is_tolerated",
    "[client_api][sync]") {
  SyncClient client("sync-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_negotiation_result();
  };
  client.set_callbacks(std::move(callbacks));
  (void)client.connect(make_connect_packet());

  client.disconnect(ReasonCode::Success);
  CHECK_FALSE(client.is_connected());

  client.disconnect(ReasonCode::Success);
  CHECK_FALSE(client.is_connected());
}

} // namespace mqtt
