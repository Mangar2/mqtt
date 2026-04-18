#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include "auth/authenticator.h"
#include "client_session/client_session.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "outbound_queue/outbound_queue.h"
#include "store/inflight_store.h"

namespace mqtt {

using namespace std::chrono_literals;

namespace {

ClientSession make_session(std::shared_ptr<IAuthenticator> authenticator,
                           InflightStore &inflight_store,
                           uint16_t receive_maximum = 10U) {
  return ClientSession("client-1", "user-1", std::move(authenticator),
                       std::make_shared<OutboundQueue>(), inflight_store, 30U,
                       receive_maximum, 8U);
}

PublishPacket decode_publish_frame(const WriteBuffer &frame) {
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  CHECK(std::holds_alternative<PublishPacket>(packet));
  return std::get<PublishPacket>(packet);
}

PubackPacket decode_puback_frame(const WriteBuffer &frame) {
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  CHECK(std::holds_alternative<PubackPacket>(packet));
  return std::get<PubackPacket>(packet);
}

PubrecPacket decode_pubrec_frame(const WriteBuffer &frame) {
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  CHECK(std::holds_alternative<PubrecPacket>(packet));
  return std::get<PubrecPacket>(packet);
}

PubrelPacket decode_pubrel_frame(const WriteBuffer &frame) {
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  CHECK(std::holds_alternative<PubrelPacket>(packet));
  return std::get<PubrelPacket>(packet);
}

PubcompPacket decode_pubcomp_frame(const WriteBuffer &frame) {
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  CHECK(std::holds_alternative<PubcompPacket>(packet));
  return std::get<PubcompPacket>(packet);
}

Message make_message(std::string_view topic_name, QoS qos_level) {
  return Message{
      .topic = Utf8String{std::string(topic_name)},
      .payload = BinaryData{{0x11U, 0x22U}},
      .qos = qos_level,
      .retain = false,
      .properties = {},
  };
}

} // namespace

TEST_CASE("client_session_ctor_sets_connected_and_exposes_accessors",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  ClientSession session = make_session(authenticator, inflight_store);

  CHECK(session.client_id() == "client-1");
  CHECK(session.username() == "user-1");
  CHECK(session.connection_state_machine().state() ==
        ConnectionState::Connected);
  CHECK(session.receive_maximum().max() == 10U);
  CHECK(session.topic_alias_table().max_aliases() == 8U);
  CHECK(session.keep_alive_timer().is_enabled());
}

TEST_CASE("on_publish_qos0_returns_routable_message_without_ack",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  ClientSession session = make_session(authenticator, inflight_store);

  const PublishPacket publish_packet{
      .dup = false,
      .qos = QoS::AtMostOnce,
      .retain = true,
      .topic = Utf8String{"sensors/temp"},
      .packet_id = std::nullopt,
      .payload = BinaryData{{0xAAU}},
      .properties = {},
  };

  const InboundPublishResult result = session.on_publish(publish_packet);
  REQUIRE(result.routable_message.has_value());
  CHECK(result.routable_message->topic.value == "sensors/temp");
  CHECK(result.routable_message->qos == QoS::AtMostOnce);
  CHECK(result.routable_message->retain);
  CHECK(result.response_frames.empty());
}

TEST_CASE("on_publish_qos1_returns_puback_frame", "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  ClientSession session = make_session(authenticator, inflight_store);

  const PublishPacket publish_packet{
      .dup = false,
      .qos = QoS::AtLeastOnce,
      .retain = false,
      .topic = Utf8String{"events/one"},
      .packet_id = 42U,
      .payload = BinaryData{{0x10U}},
      .properties = {},
  };

  const InboundPublishResult result = session.on_publish(publish_packet);
  REQUIRE(result.routable_message.has_value());
  REQUIRE(result.response_frames.size() == 1U);

  const PubackPacket puback_packet =
      decode_puback_frame(result.response_frames[0]);
  CHECK(puback_packet.packet_id == 42U);
}

TEST_CASE("on_publish_qos2_duplicate_suppresses_routing", "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  ClientSession session = make_session(authenticator, inflight_store);

  const PublishPacket publish_packet{
      .dup = false,
      .qos = QoS::ExactlyOnce,
      .retain = false,
      .topic = Utf8String{"events/two"},
      .packet_id = 7U,
      .payload = BinaryData{{0x77U}},
      .properties = {},
  };

  const InboundPublishResult first_result = session.on_publish(publish_packet);
  const InboundPublishResult second_result = session.on_publish(publish_packet);

  CHECK(first_result.routable_message.has_value());
  CHECK_FALSE(second_result.routable_message.has_value());
  REQUIRE(first_result.response_frames.size() == 1U);
  REQUIRE(second_result.response_frames.size() == 1U);
  CHECK(decode_pubrec_frame(first_result.response_frames[0]).packet_id == 7U);
  CHECK(decode_pubrec_frame(second_result.response_frames[0]).packet_id == 7U);
}

TEST_CASE("on_pubrel_returns_pubcomp_frame_for_inbound_qos2",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  ClientSession session = make_session(authenticator, inflight_store);

  const PublishPacket publish_packet{
      .dup = false,
      .qos = QoS::ExactlyOnce,
      .retain = false,
      .topic = Utf8String{"events/three"},
      .packet_id = 19U,
      .payload = BinaryData{{0x19U}},
      .properties = {},
  };
  (void)session.on_publish(publish_packet);

  const WriteBuffer pubcomp_frame =
      session.on_pubrel(PubrelPacket{.packet_id = 19U, .properties = {}});
  const PubcompPacket pubcomp_packet = decode_pubcomp_frame(pubcomp_frame);
  CHECK(pubcomp_packet.packet_id == 19U);
}

TEST_CASE("drain_outbound_qos0_encodes_publish_without_packet_id",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 10U, 8U);

  REQUIRE(queue->push(make_message("out/qos0", QoS::AtMostOnce)));
  const std::vector<WriteBuffer> frames = session.drain_outbound();
  REQUIRE(frames.size() == 1U);

  const PublishPacket publish_packet = decode_publish_frame(frames[0]);
  CHECK(publish_packet.topic.value == "out/qos0");
  CHECK(publish_packet.qos == QoS::AtMostOnce);
  CHECK_FALSE(publish_packet.packet_id.has_value());
}

TEST_CASE("drain_outbound_receive_maximum_pauses_and_resumes_after_puback",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 1U, 8U);

  REQUIRE(queue->push(make_message("out/qos1/first", QoS::AtLeastOnce)));
  REQUIRE(queue->push(make_message("out/qos1/second", QoS::AtLeastOnce)));

  const std::vector<WriteBuffer> first_frames = session.drain_outbound();
  REQUIRE(first_frames.size() == 1U);
  const PublishPacket first_publish = decode_publish_frame(first_frames[0]);
  REQUIRE(first_publish.packet_id.has_value());

  const std::vector<WriteBuffer> paused_frames = session.drain_outbound();
  CHECK(paused_frames.empty());

  session.on_puback(PubackPacket{.packet_id = first_publish.packet_id.value(),
                                 .properties = {}});

  const std::vector<WriteBuffer> resumed_frames = session.drain_outbound();
  REQUIRE(resumed_frames.size() == 1U);
  const PublishPacket second_publish = decode_publish_frame(resumed_frames[0]);
  CHECK(second_publish.topic.value == "out/qos1/second");
}

TEST_CASE("on_pubrec_returns_pubrel_and_on_pubcomp_releases_slot",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 1U, 8U);

  REQUIRE(queue->push(make_message("out/qos2/first", QoS::ExactlyOnce)));
  const std::vector<WriteBuffer> first_frames = session.drain_outbound();
  REQUIRE(first_frames.size() == 1U);
  const PublishPacket first_publish = decode_publish_frame(first_frames[0]);
  REQUIRE(first_publish.packet_id.has_value());

  const WriteBuffer pubrel_frame = session.on_pubrec(PubrecPacket{
      .packet_id = first_publish.packet_id.value(), .properties = {}});
  const PubrelPacket pubrel_packet = decode_pubrel_frame(pubrel_frame);
  CHECK(pubrel_packet.packet_id == first_publish.packet_id.value());

  REQUIRE(queue->push(make_message("out/qos2/second", QoS::ExactlyOnce)));
  CHECK(session.drain_outbound().empty());

  session.on_pubcomp(PubcompPacket{.packet_id = first_publish.packet_id.value(),
                                   .properties = {}});

  const std::vector<WriteBuffer> resumed_frames = session.drain_outbound();
  REQUIRE(resumed_frames.size() == 1U);
  const PublishPacket second_publish = decode_publish_frame(resumed_frames[0]);
  CHECK(second_publish.topic.value == "out/qos2/second");
}

TEST_CASE("on_auth_routes_to_reauthenticate_when_reason_is_reauthenticate",
          "[client_session]") {
  InflightStore inflight_store;

  std::size_t authenticate_calls = 0U;
  auto authenticator = std::make_shared<CallbackAuthenticator>(
      [&authenticate_calls](const ConnectPacket &) {
        ++authenticate_calls;
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      },
      [](const AuthPacket &) {
        return AuthResult{.status = AuthStatus::Failure,
                          .reason_code = ReasonCode::NotAuthorized,
                          .auth_data = std::nullopt};
      });

  ClientSession session = make_session(authenticator, inflight_store);

  ConnectPacket connect_packet;
  connect_packet.properties.push_back(Property{
      .id = PropertyId::AuthenticationMethod,
      .value = Utf8String{"token-auth"},
  });
  REQUIRE(session.initiate_auth(connect_packet).status == AuthStatus::Success);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ReAuthenticate;
  auth_packet.properties.push_back(Property{
      .id = PropertyId::AuthenticationMethod,
      .value = Utf8String{"token-auth"},
  });

  const AuthResult auth_result = session.on_auth(auth_packet);
  CHECK(auth_result.status == AuthStatus::Success);
  CHECK(auth_result.reason_code == ReasonCode::Success);
  CHECK(authenticate_calls == 2U);
}

TEST_CASE("drain_outbound_retransmits_overdue_qos1", "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 10U, 8U, 10ms);

  REQUIRE(queue->push(make_message("out/qos1/retry", QoS::AtLeastOnce)));
  const std::vector<WriteBuffer> first_frames = session.drain_outbound();
  REQUIRE(first_frames.size() == 1U);
  const PublishPacket first_publish = decode_publish_frame(first_frames[0]);
  REQUIRE(first_publish.packet_id.has_value());
  CHECK_FALSE(first_publish.dup);

  std::this_thread::sleep_for(20ms);

  const std::vector<WriteBuffer> retransmit_frames = session.drain_outbound();
  REQUIRE(retransmit_frames.size() == 1U);
  const PublishPacket retransmit_publish =
      decode_publish_frame(retransmit_frames[0]);
  CHECK(retransmit_publish.packet_id == first_publish.packet_id);
  CHECK(retransmit_publish.dup);
}

TEST_CASE("drain_outbound_retransmits_overdue_qos2_waiting_for_pubrec",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 10U, 8U, 10ms);

  REQUIRE(queue->push(make_message("out/qos2/retry", QoS::ExactlyOnce)));
  const std::vector<WriteBuffer> first_frames = session.drain_outbound();
  REQUIRE(first_frames.size() == 1U);
  const PublishPacket first_publish = decode_publish_frame(first_frames[0]);
  REQUIRE(first_publish.packet_id.has_value());
  CHECK_FALSE(first_publish.dup);

  std::this_thread::sleep_for(20ms);

  const std::vector<WriteBuffer> retransmit_frames = session.drain_outbound();
  REQUIRE(retransmit_frames.size() == 1U);
  const PublishPacket retransmit_publish =
      decode_publish_frame(retransmit_frames[0]);
  CHECK(retransmit_publish.packet_id == first_publish.packet_id);
  CHECK(retransmit_publish.dup);
  CHECK(retransmit_publish.qos == QoS::ExactlyOnce);
}

TEST_CASE("drain_outbound_retransmits_overdue_qos2_waiting_for_pubcomp",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 10U, 8U, 10ms);

  REQUIRE(queue->push(make_message("out/qos2/pubrel", QoS::ExactlyOnce)));
  const std::vector<WriteBuffer> first_frames = session.drain_outbound();
  REQUIRE(first_frames.size() == 1U);
  const PublishPacket first_publish = decode_publish_frame(first_frames[0]);
  REQUIRE(first_publish.packet_id.has_value());

  (void)session.on_pubrec(PubrecPacket{
      .packet_id = first_publish.packet_id.value(), .properties = {}});

  std::this_thread::sleep_for(20ms);

  const std::vector<WriteBuffer> retransmit_frames = session.drain_outbound();
  REQUIRE(retransmit_frames.size() == 1U);
  const PubrelPacket retransmit_pubrel =
      decode_pubrel_frame(retransmit_frames[0]);
  CHECK(retransmit_pubrel.packet_id == first_publish.packet_id.value());
}

TEST_CASE("drain_outbound_skips_publish_above_maximum_packet_size",
          "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  auto queue = std::make_shared<OutboundQueue>();
  ClientSession session("client-1", "user-1", authenticator, queue,
                        inflight_store, 30U, 10U, 8U, 20s, 32U);

  Message oversized = make_message("out/max", QoS::AtMostOnce);
  oversized.payload.data.assign(128U, 0x42U);
  REQUIRE(queue->push(oversized));

  const std::vector<WriteBuffer> frames = session.drain_outbound();
  CHECK(frames.empty());
}

TEST_CASE("client_session_ctor_rejects_null_queue", "[client_session]") {
  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });

  CHECK_THROWS_AS(
      ClientSession("client-1", "user-1", authenticator, nullptr,
                    inflight_store, 30U, 10U, 8U),
      std::invalid_argument);
}

TEST_CASE("on_auth_routes_to_handler_when_not_reauthenticate",
          "[client_session]") {
  InflightStore inflight_store;

  auto authenticator = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        BinaryData challenge;
        challenge.data = {0x01U};
        return AuthResult{.status = AuthStatus::Continue,
                          .reason_code = ReasonCode::ContinueAuthentication,
                          .auth_data = challenge};
      },
      [](const AuthPacket &) {
        return AuthResult{.status = AuthStatus::Failure,
                          .reason_code = ReasonCode::BadAuthenticationMethod,
                          .auth_data = std::nullopt};
      });

  ClientSession session = make_session(authenticator, inflight_store);

  ConnectPacket connect_packet;
  connect_packet.properties.push_back(Property{
      .id = PropertyId::AuthenticationMethod,
      .value = Utf8String{"token-auth"},
  });
  REQUIRE(session.initiate_auth(connect_packet).status == AuthStatus::Continue);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(Property{
      .id = PropertyId::AuthenticationMethod,
      .value = Utf8String{"token-auth"},
  });

  const AuthResult auth_result = session.on_auth(auth_packet);
  CHECK(auth_result.status == AuthStatus::Failure);
  CHECK(auth_result.reason_code == ReasonCode::BadAuthenticationMethod);
}

} // namespace mqtt
