#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "codec/packet/connect_codec.h"
#include "connection/connection_session.h"
#include "connection/close_step.h"
#include "connection/decode_step.h"
#include "connection/handshake_step.h"
#include "connection/outbound_drain_step.h"
#include "connection/runtime_step.h"
#include "data_model/message/message.h"
#include "data_model/packet/control_packets.h"
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
