#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <utility>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "client_session/client_session.h"
#include "connection/connection_session.h"
#include "connection/close_step.h"
#include "connection/decode_step.h"
#include "connection/handshake_step.h"
#include "connection/outbound_drain_step.h"
#include "connection/runtime_step.h"
#include "data_model/message/message.h"
#include "data_model/packet/control_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include "network/tcp_connection.h"

using namespace mqtt;

namespace {

BrokerConfig make_step_test_config() {
  BrokerConfig config;
  config.mqtt_port = 0U;
  config.ws_port = 0U;
  config.allow_anonymous = true;
  config.persistence_mode = PersistenceMode::Off;
  config.tick_interval_ms = 100U;
  return config;
}

ConnectPacket make_connect_packet() {
  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"step-client"};
  connect_packet.keep_alive = 10U;
  connect_packet.clean_start = true;
  return connect_packet;
}

ConnectionSession make_session(const BrokerConfig &config) {
  auto connection =
      std::make_unique<TcpConnection>(static_cast<SocketHandle>(k_invalid_socket));
  return ConnectionSession(std::move(connection), nullptr, false, config);
}

} // namespace

TEST_CASE("decode_one_packet_returns_need_more_on_empty_stream", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  CHECK(decode_one_packet(session, broker) == DecodeOutcome::NeedMore);

  broker.shutdown();
}

TEST_CASE("decode_one_packet_processes_connect_packet_from_stream", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  WriteBuffer connect_frame;
  encode_connect(connect_frame, make_connect_packet());
  session.stream_buffer().append(connect_frame);

  CHECK(decode_one_packet(session, broker) == DecodeOutcome::Processed);
  CHECK(session.phase() == ConnectionSession::Phase::Connected);

  broker.shutdown();
}

TEST_CASE("decode_one_packet_reports_protocol_error_on_malformed_stream",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const std::vector<uint8_t> malformed_packet{0x10U, 0x00U};
  session.stream_buffer().append(malformed_packet);

  CHECK(decode_one_packet(session, broker) == DecodeOutcome::ProtocolError);

  broker.shutdown();
}

TEST_CASE("decode_one_packet_returns_disconnected_for_non_connected_phase",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  session.set_phase(ConnectionSession::Phase::Closing);
  WriteBuffer connect_frame;
  encode_connect(connect_frame, make_connect_packet());
  session.stream_buffer().append(connect_frame);

  CHECK(decode_one_packet(session, broker) == DecodeOutcome::Disconnected);

  broker.shutdown();
}

TEST_CASE("decode_one_packet_runtime_pingreq_returns_processed", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);
  session.clear_pending_write_frames();

  const std::array<uint8_t, 2> pingreq_frame{0xC0U, 0x00U};
  session.stream_buffer().append(pingreq_frame);
  CHECK(decode_one_packet(session, broker) == DecodeOutcome::Processed);

  broker.shutdown();
}

TEST_CASE("decode_one_packet_runtime_codec_error_enqueues_disconnect",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);
  session.clear_pending_write_frames();

  const std::vector<uint8_t> malformed_publish{0x36U, 0x02U, 0x00U, 0x01U};
  session.stream_buffer().append(malformed_publish);

  CHECK(decode_one_packet(session, broker) == DecodeOutcome::ProtocolError);
  CHECK_FALSE(session.pending_write_frames().empty());

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_rejects_non_connect_packet", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket pingreq = PingreqPacket{};

  CHECK(process_handshake_packet(session, broker, pingreq) ==
        HandshakeOutcome::Rejected);
  CHECK_FALSE(session.pending_write_frames().empty());

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_accepts_connect_and_installs_client_session",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();

  CHECK(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);
  CHECK(session.phase() == ConnectionSession::Phase::Connected);
  REQUIRE(session.client_session() != nullptr);
  CHECK_FALSE(session.pending_write_frames().empty());

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_rejects_connect_on_auth_failure",
          "[connection]") {
  BrokerConfig config = make_step_test_config();
  config.allow_anonymous = false;
  config.password_credentials.push_back(
      PasswordCredentialConfig{.username = "u", .password = "p"});

  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  CHECK(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::Rejected);

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_auth_branch_handles_missing_exchange",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  session.connect_result().client_id = "auth-step-client";

  const AnyPacket auth_packet = AuthPacket{};
  CHECK(process_handshake_packet(session, broker, auth_packet) ==
        HandshakeOutcome::Rejected);

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_auth_method_connect_not_rejected", "[connection]") {
  BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  ConnectPacket connect_packet = make_connect_packet();
  connect_packet.properties.push_back(Property{
      .id = PropertyId::AuthenticationMethod,
      .value = Utf8String{"token-auth"},
  });

  const HandshakeOutcome outcome =
      process_handshake_packet(session, broker, connect_packet);
  CHECK(outcome != HandshakeOutcome::Rejected);
  CHECK_FALSE(session.pending_write_frames().empty());

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_auth_success_path", "[connection]") {
  BrokerConfig config = make_step_test_config();
  config.allow_anonymous = false;
  config.server_keep_alive = 9U;
  config.password_credentials.push_back(
      PasswordCredentialConfig{.username = "u", .password = "p"});

  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  ConnectPacket connect_packet = make_connect_packet();
  connect_packet.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                               .value = Utf8String{"PLAIN"}});

  CHECK(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::Continuing);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                            .value = Utf8String{"PLAIN"}});
  auth_packet.properties.push_back(Property{.id = PropertyId::AuthenticationData,
                                            .value = BinaryData::from_string("u:p")});

  CHECK(process_handshake_packet(session, broker, auth_packet) ==
        HandshakeOutcome::ConnectAccepted);
  REQUIRE(session.client_session() != nullptr);

  broker.shutdown();
}

TEST_CASE("process_handshake_packet_connect_resume_sets_session_present", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession first_session = make_session(config);
  ConnectPacket first_connect = make_connect_packet();
  first_connect.clean_start = false;
  first_connect.properties.push_back(Property{.id = PropertyId::SessionExpiryInterval,
                                              .value = FourByteInteger{120U}});
  REQUIRE(process_handshake_packet(first_session, broker, first_connect) ==
          HandshakeOutcome::ConnectAccepted);

  ConnectionSession resumed_session = make_session(config);
  ConnectPacket resumed_connect = make_connect_packet();
  resumed_connect.clean_start = false;
  resumed_connect.properties.push_back(Property{.id = PropertyId::SessionExpiryInterval,
                                                .value = FourByteInteger{120U}});
  CHECK(process_handshake_packet(resumed_session, broker, resumed_connect) ==
        HandshakeOutcome::ConnectAccepted);
  CHECK(resumed_session.connect_result().session_present);

  broker.shutdown();
}

TEST_CASE("process_runtime_packet_pingreq_enqueues_pingresp", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);

  session.clear_pending_write_frames();
  const AnyPacket pingreq = PingreqPacket{};
  CHECK(process_runtime_packet(session, broker, pingreq) ==
        RuntimeOutcome::Continuing);
  CHECK(session.pending_write_frames().size() == 1U);

  broker.shutdown();
}

  TEST_CASE("process_runtime_packet_publish_qos1_and_qos2_response_paths",
        "[connection]") {
    const BrokerConfig config = make_step_test_config();
    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    const AnyPacket connect_packet = make_connect_packet();
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);

    session.clear_pending_write_frames();
    PublishPacket qos1_publish;
    qos1_publish.topic = Utf8String{"rt/qos1"};
    qos1_publish.payload = BinaryData::from_string("payload-1");
    qos1_publish.qos = QoS::AtLeastOnce;
    qos1_publish.packet_id = 11U;
    CHECK(process_runtime_packet(session, broker, qos1_publish) ==
      RuntimeOutcome::Continuing);
    CHECK_FALSE(session.pending_write_frames().empty());

    session.clear_pending_write_frames();
    PublishPacket qos2_publish;
    qos2_publish.topic = Utf8String{"rt/qos2"};
    qos2_publish.payload = BinaryData::from_string("payload-2");
    qos2_publish.qos = QoS::ExactlyOnce;
    qos2_publish.packet_id = 12U;
    CHECK(process_runtime_packet(session, broker, qos2_publish) ==
      RuntimeOutcome::Continuing);
    CHECK_FALSE(session.pending_write_frames().empty());

    broker.shutdown();
  }

  TEST_CASE("process_runtime_packet_qos2_receive_maximum_exceeded_disconnects",
        "[connection]") {
    BrokerConfig config = make_step_test_config();
    config.receive_maximum = 1U;
    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    const AnyPacket connect_packet = make_connect_packet();
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);

    REQUIRE(session.inbound_receive_window().acquire());

    PublishPacket qos2_publish;
    qos2_publish.topic = Utf8String{"rt/inbound-qos2"};
    qos2_publish.payload = BinaryData::from_string("payload");
    qos2_publish.qos = QoS::ExactlyOnce;
    qos2_publish.packet_id = 21U;

    CHECK(process_runtime_packet(session, broker, qos2_publish) ==
      RuntimeOutcome::DisconnectError);
    CHECK(session.disconnect_state().reason_code ==
      ReasonCode::ReceiveMaximumExceeded);

    broker.shutdown();
  }

TEST_CASE("process_runtime_packet_puback_pubrec_pubcomp_paths", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);
  REQUIRE(session.client_session() != nullptr);
  ClientSession &client_session = *session.client_session();

  Message qos1_message;
  qos1_message.topic = Utf8String{"ack/qos1"};
  qos1_message.payload = BinaryData::from_string("q1");
  qos1_message.qos = QoS::AtLeastOnce;
  REQUIRE(client_session.outbound_queue()->push(qos1_message));

  Message qos2_message;
  qos2_message.topic = Utf8String{"ack/qos2"};
  qos2_message.payload = BinaryData::from_string("q2");
  qos2_message.qos = QoS::ExactlyOnce;
  REQUIRE(client_session.outbound_queue()->push(qos2_message));

  const std::vector<WriteBuffer> outbound_frames = client_session.drain_outbound();
  std::optional<uint16_t> qos1_packet_id;
  std::optional<uint16_t> qos2_packet_id;
  for (const WriteBuffer &frame : outbound_frames) {
    ReadBuffer reader(std::span<const uint8_t>(frame.data(), frame.size()));
    const AnyPacket decoded = read_packet(reader);
    if (!std::holds_alternative<PublishPacket>(decoded)) {
      continue;
    }
    const PublishPacket &publish_packet = std::get<PublishPacket>(decoded);
    if (!publish_packet.packet_id.has_value()) {
      continue;
    }
    if (publish_packet.qos == QoS::AtLeastOnce) {
      qos1_packet_id = publish_packet.packet_id;
    } else if (publish_packet.qos == QoS::ExactlyOnce) {
      qos2_packet_id = publish_packet.packet_id;
    }
  }
  REQUIRE(qos1_packet_id.has_value());
  REQUIRE(qos2_packet_id.has_value());

  CHECK(process_runtime_packet(session, broker,
                               PubackPacket{.packet_id = *qos1_packet_id,
                                            .reason_code = ReasonCode::Success,
                                            .properties = {}}) ==
        RuntimeOutcome::Continuing);

  session.clear_pending_write_frames();
  CHECK(process_runtime_packet(session, broker,
                               PubrecPacket{.packet_id = *qos2_packet_id,
                                            .reason_code = ReasonCode::Success,
                                            .properties = {}}) ==
        RuntimeOutcome::Continuing);
  CHECK_FALSE(session.pending_write_frames().empty());

  CHECK(process_runtime_packet(session, broker,
                               PubcompPacket{.packet_id = *qos2_packet_id,
                                             .reason_code = ReasonCode::Success,
                                             .properties = {}}) ==
        RuntimeOutcome::Continuing);

  broker.shutdown();
}

TEST_CASE("process_runtime_packet_disconnect_invalid_expiry_override_returns_disconnect_error",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);

  DisconnectPacket disconnect_packet;
  disconnect_packet.reason_code = ReasonCode::Success;
  disconnect_packet.properties.push_back(Property{.id = PropertyId::SessionExpiryInterval,
                                                  .value = FourByteInteger{10U}});
  CHECK(process_runtime_packet(session, broker, disconnect_packet) ==
        RuntimeOutcome::DisconnectError);

  broker.shutdown();
}

TEST_CASE("process_runtime_packet_auth_failure_returns_disconnect_error",
          "[connection]") {
  BrokerConfig config = make_step_test_config();
  config.allow_anonymous = false;
  config.password_credentials.push_back(
      PasswordCredentialConfig{.username = "u", .password = "p"});

  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  ConnectPacket connect_packet = make_connect_packet();
  connect_packet.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                               .value = Utf8String{"PLAIN"}});
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::Continuing);

  AuthPacket auth_complete;
  auth_complete.reason_code = ReasonCode::ContinueAuthentication;
  auth_complete.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                              .value = Utf8String{"PLAIN"}});
  auth_complete.properties.push_back(Property{.id = PropertyId::AuthenticationData,
                                              .value = BinaryData::from_string("u:p")});
  REQUIRE(process_handshake_packet(session, broker, auth_complete) ==
          HandshakeOutcome::ConnectAccepted);

  AuthPacket bad_runtime_auth;
  bad_runtime_auth.reason_code = ReasonCode::ReAuthenticate;
  bad_runtime_auth.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                                 .value = Utf8String{"WRONG"}});
  CHECK(process_runtime_packet(session, broker, bad_runtime_auth) ==
        RuntimeOutcome::DisconnectError);

  broker.shutdown();
}

  TEST_CASE("process_runtime_packet_auth_status_failure_returns_disconnect_error",
        "[connection]") {
    BrokerConfig config = make_step_test_config();
    config.allow_anonymous = false;
    config.password_credentials.push_back(
    PasswordCredentialConfig{.username = "u", .password = "p"});

    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    ConnectPacket connect_packet = make_connect_packet();
    connect_packet.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                     .value = Utf8String{"PLAIN"}});
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::Continuing);

    AuthPacket auth_complete;
    auth_complete.reason_code = ReasonCode::ContinueAuthentication;
    auth_complete.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                    .value = Utf8String{"PLAIN"}});
    auth_complete.properties.push_back(Property{.id = PropertyId::AuthenticationData,
                    .value = BinaryData::from_string("u:p")});
    REQUIRE(process_handshake_packet(session, broker, auth_complete) ==
        HandshakeOutcome::ConnectAccepted);

    AuthPacket bad_runtime_auth;
    bad_runtime_auth.reason_code = ReasonCode::ContinueAuthentication;
    bad_runtime_auth.properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                   .value = Utf8String{"PLAIN"}});
    bad_runtime_auth.properties.push_back(Property{.id = PropertyId::AuthenticationData,
                   .value = BinaryData::from_string("u:wrong")});

    CHECK(process_runtime_packet(session, broker, bad_runtime_auth) ==
      RuntimeOutcome::DisconnectError);
      CHECK(session.disconnect_state().clean_disconnect);

    broker.shutdown();
  }

  TEST_CASE("process_runtime_packet_publish_subscribe_unsubscribe", "[connection]") {
    const BrokerConfig config = make_step_test_config();
    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    const AnyPacket connect_packet = make_connect_packet();
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);
    session.clear_pending_write_frames();

    PublishPacket publish_packet;
    publish_packet.topic = Utf8String{"step/topic"};
    publish_packet.payload = BinaryData::from_string("msg");
    publish_packet.qos = QoS::AtMostOnce;
    CHECK(process_runtime_packet(session, broker, publish_packet) ==
      RuntimeOutcome::Continuing);

    SubscribePacket subscribe_packet;
    subscribe_packet.packet_id = 1U;
    subscribe_packet.filters.push_back(
    SubscribeFilter{.topic_filter = Utf8String{"step/#"},
            .options = SubscribeOptions{}});
    CHECK(process_runtime_packet(session, broker, subscribe_packet) ==
      RuntimeOutcome::Continuing);

    UnsubscribePacket unsubscribe_packet;
    unsubscribe_packet.packet_id = 2U;
    unsubscribe_packet.topic_filters.push_back(Utf8String{"step/#"});
    CHECK(process_runtime_packet(session, broker, unsubscribe_packet) ==
      RuntimeOutcome::Continuing);

    broker.shutdown();
  }

  TEST_CASE("process_runtime_packet_disconnect_clean", "[connection]") {
    const BrokerConfig config = make_step_test_config();
    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    const AnyPacket connect_packet = make_connect_packet();
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);

    DisconnectPacket disconnect_packet;
    disconnect_packet.reason_code = ReasonCode::Success;
    CHECK(process_runtime_packet(session, broker, disconnect_packet) ==
      RuntimeOutcome::DisconnectClean);

    broker.shutdown();
  }

  TEST_CASE("process_runtime_packet_default_path_reports_protocol_error",
        "[connection]") {
    const BrokerConfig config = make_step_test_config();
    Broker broker(config);
    broker.startup();

    ConnectionSession session = make_session(config);
    const AnyPacket connect_packet = make_connect_packet();
    REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);

    const AnyPacket unexpected = ConnackPacket{};
    CHECK(process_runtime_packet(session, broker, unexpected) ==
      RuntimeOutcome::DisconnectError);

    broker.shutdown();
  }

    TEST_CASE("process_runtime_packet_unknown_pubrel_returns_packet_identifier_not_found",
        "[connection]") {
      const BrokerConfig config = make_step_test_config();
      Broker broker(config);
      broker.startup();

      ConnectionSession session = make_session(config);
      const AnyPacket connect_packet = make_connect_packet();
      REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
        HandshakeOutcome::ConnectAccepted);

      session.clear_pending_write_frames();
      const AnyPacket pubrel = PubrelPacket{.packet_id = 41U,
              .reason_code = ReasonCode::Success,
              .properties = {}};
      CHECK(process_runtime_packet(session, broker, pubrel) ==
      RuntimeOutcome::Continuing);
      REQUIRE(session.pending_write_frames().size() == 1U);

      const WriteBuffer &frame = session.pending_write_frames().front();
      ReadBuffer reader(std::span<const uint8_t>(frame.data(), frame.size()));
      const AnyPacket decoded = read_packet(reader);
      REQUIRE(std::holds_alternative<PubcompPacket>(decoded));
      const PubcompPacket &pubcomp = std::get<PubcompPacket>(decoded);
      CHECK(pubcomp.packet_id == 41U);
      CHECK(pubcomp.reason_code == ReasonCode::PacketIdentifierNotFound);

      broker.shutdown();
    }

TEST_CASE("drain_outbound_to_write_buffer_moves_client_session_frames",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);

  REQUIRE(session.outbound_queue() != nullptr);
  Message msg;
  msg.topic = Utf8String{"step/topic"};
  msg.payload = BinaryData::from_string("abc");
  msg.qos = QoS::AtMostOnce;
  msg.retain = false;
  REQUIRE(session.outbound_queue()->push(std::move(msg)));

  const std::size_t before = session.pending_write_frames().size();
  drain_outbound_to_write_buffer(session, broker);
  CHECK(session.pending_write_frames().size() > before);

  broker.shutdown();
}

TEST_CASE("process_runtime_packet_without_client_session_reports_error",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  session.set_phase(ConnectionSession::Phase::Connected);
  CHECK(process_runtime_packet(session, broker, PingreqPacket{}) ==
        RuntimeOutcome::DisconnectError);

  broker.shutdown();
}

TEST_CASE("process_runtime_packet_auth_failure_disconnects", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ReAuthenticate;
  CHECK(process_runtime_packet(session, broker, auth_packet) ==
        RuntimeOutcome::DisconnectError);

  broker.shutdown();
}

TEST_CASE("finalize_close_handles_clean_and_lost_paths", "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  const AnyPacket connect_packet = make_connect_packet();
  REQUIRE(process_handshake_packet(session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);

  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::Success;
  CHECK_NOTHROW(finalize_close(session, broker));

  ConnectionSession second_session = make_session(config);
  REQUIRE(process_handshake_packet(second_session, broker, connect_packet) ==
          HandshakeOutcome::ConnectAccepted);
  second_session.disconnect_state().clean_disconnect = false;
  CHECK_NOTHROW(finalize_close(second_session, broker));

  broker.shutdown();
}

TEST_CASE("finalize_close_uses_connect_result_client_id_without_client_session",
          "[connection]") {
  const BrokerConfig config = make_step_test_config();
  Broker broker(config);
  broker.startup();

  ConnectionSession session = make_session(config);
  session.connect_result().client_id = "close-only-client";
  session.disconnect_state().clean_disconnect = true;
  session.disconnect_state().reason_code = ReasonCode::Success;

  CHECK_NOTHROW(finalize_close(session, broker));
  broker.shutdown();
}
