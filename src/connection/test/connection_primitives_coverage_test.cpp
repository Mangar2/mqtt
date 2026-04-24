#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "codec/codec_error.h"
#include "connection/connection_error.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_state.h"
#include "connection/keep_alive_timer.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"

using namespace mqtt;

TEST_CASE("connection_state_machine_transitions_and_guards", "[connection]") {
  ConnectionStateMachine state_machine;
  CHECK(state_machine.state() == ConnectionState::Connecting);
  CHECK_FALSE(state_machine.is_connected());
  CHECK_THROWS(state_machine.enforce_not_connecting());

  state_machine.on_connect();
  CHECK(state_machine.state() == ConnectionState::Connected);
  CHECK(state_machine.is_connected());
  CHECK_NOTHROW(state_machine.enforce_not_connecting());
  CHECK_THROWS(state_machine.on_connect());

  state_machine.on_disconnect();
  CHECK(state_machine.state() == ConnectionState::Disconnecting);
  CHECK_THROWS(state_machine.enforce_not_connecting());

  state_machine.on_connection_lost();
  CHECK(state_machine.state() == ConnectionState::Closed);
  CHECK_THROWS(state_machine.on_disconnect());

  ConnectionStateMachine second;
  second.close();
  CHECK(second.state() == ConnectionState::Closed);
  CHECK_THROWS(second.on_connect());
}

TEST_CASE("keep_alive_timer_enabled_and_disabled_paths", "[connection]") {
  KeepAliveTimer disabled_timer(0U);
  CHECK_FALSE(disabled_timer.is_enabled());
  CHECK_FALSE(disabled_timer.is_expired());
  disabled_timer.reset();
  CHECK_FALSE(disabled_timer.is_expired());

  KeepAliveTimer enabled_timer(1U);
  CHECK(enabled_timer.is_enabled());
  enabled_timer.reset();
  CHECK_FALSE(enabled_timer.is_expired());
}

TEST_CASE("keep_alive_deadline_reflects_enabled_and_disabled_state",
          "[connection]") {
  KeepAliveTimer disabled_timer(0U);
  CHECK_FALSE(disabled_timer.deadline().has_value());

  KeepAliveTimer enabled_timer(10U);
  const auto enabled_deadline = enabled_timer.deadline();
  REQUIRE(enabled_deadline.has_value());
  CHECK(*enabled_deadline > std::chrono::steady_clock::now());
}

TEST_CASE("topic_alias_table_all_core_paths", "[connection]") {
  TopicAliasTable table(2U);
  CHECK(table.max_aliases() == 2U);

  table.set_inbound(1U, "topic/one");
  CHECK(table.get_inbound(1U) == "topic/one");
  CHECK_THROWS(table.get_inbound(2U));

  table.set_outbound("topic/two", 2U);
  REQUIRE(table.get_outbound("topic/two").has_value());
  CHECK(*table.get_outbound("topic/two") == 2U);
  CHECK_FALSE(table.get_outbound("missing").has_value());

  CHECK_THROWS(table.set_inbound(0U, "x"));
  CHECK_THROWS(table.set_outbound("x", 3U));
  CHECK_THROWS(table.get_inbound(3U));

  table.reset();
  CHECK_FALSE(table.get_outbound("topic/two").has_value());
}

TEST_CASE("receive_maximum_acquire_release_and_limits", "[connection]") {
  ReceiveMaximum receive_maximum(0U);
  CHECK(receive_maximum.max() == 65535U);

  ReceiveMaximum limited(2U);
  CHECK(limited.acquire());
  CHECK(limited.acquire());
  CHECK_FALSE(limited.acquire());
  CHECK(limited.is_paused());
  CHECK(limited.available() == 0U);
  limited.release();
  CHECK_FALSE(limited.is_paused());
  CHECK(limited.available() == 1U);
  limited.release();
  CHECK_THROWS(limited.release());
}

TEST_CASE("connection_error_codec_mapping_paths", "[connection]") {
  const ConnectionException exception(ConnectionError::InvalidState,
              "invalid state");
  CHECK(exception.error() == ConnectionError::InvalidState);

  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidProtocolVersion) ==
        ReasonCode::UnsupportedProtocolVersion);
  CHECK(map_codec_error_to_connect_reason(CodecError::BufferTooShort) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::StringTooLong) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(
    CodecError::VariableByteIntegerOverflow) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidPropertyId) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::PropertyTypeMismatch) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::DuplicateProperty) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::PropertyNotAllowed) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidPacketType) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidFlags) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidProtocolName) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidQoS) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(CodecError::MalformedPacket) ==
        ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_connect_reason(static_cast<CodecError>(255)) ==
        ReasonCode::ProtocolError);

  CHECK(map_codec_error_to_runtime_reason(CodecError::DuplicateProperty) ==
        ReasonCode::ProtocolError);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidPacketType) ==
    ReasonCode::ProtocolError);
  CHECK(map_codec_error_to_runtime_reason(CodecError::BufferTooShort) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::StringTooLong) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(
    CodecError::VariableByteIntegerOverflow) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidPropertyId) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::PropertyTypeMismatch) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::PropertyNotAllowed) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidFlags) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidProtocolName) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidProtocolVersion) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidQoS) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::MalformedPacket) ==
        ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(static_cast<CodecError>(255)) ==
        ReasonCode::ProtocolError);
}

TEST_CASE("connection_flow_support_property_helpers", "[connection]") {
  const std::vector<Property> properties{
      Property{.id = PropertyId::SessionExpiryInterval,
               .value = FourByteInteger{15U}},
      Property{.id = PropertyId::MaximumPacketSize,
               .value = FourByteInteger{1024U}},
      Property{.id = PropertyId::ReceiveMaximum, .value = TwoByteInteger{8U}},
  };

  CHECK(find_session_expiry_override(properties).value_or(0U) == 15U);
  CHECK(find_maximum_packet_size(properties).value_or(0U) == 1024U);
  CHECK(find_receive_maximum(properties).value_or(0U) == 8U);

  const auto auth_properties =
      build_auth_properties("m", BinaryData::from_string("x"), true);
  CHECK(auth_properties.size() == 2U);

  ConnectPacket connect_packet;
  connect_packet.properties.push_back(Property{.id = PropertyId::RequestProblemInformation,
                                               .value = static_cast<uint8_t>(1U)});
  const auto reason_properties =
      build_protocol_error_disconnect_properties(connect_packet, "bad");
  CHECK(reason_properties.size() == 1U);
}

TEST_CASE("connection_flow_support_encoding_and_decode_helpers", "[connection]") {
  const WriteBuffer connack =
      encode_connack_packet(ConnackPacket{.session_present = false,
                                          .reason_code = ReasonCode::Success,
                                          .properties = {}});
  const WriteBuffer pingresp = encode_pingresp_packet();
  const WriteBuffer disconnect = encode_disconnect_packet(ReasonCode::Success);
  const WriteBuffer auth = encode_auth_packet(ReasonCode::ContinueAuthentication);
  const WriteBuffer suback =
      encode_suback_packet(SubackPacket{.packet_id = 1U,
                      .reason_codes = {ReasonCode::Success},
                                        .properties = {}});
  const WriteBuffer unsuback =
      encode_unsuback_packet(UnsubackPacket{.packet_id = 2U,
                                            .reason_codes = {ReasonCode::Success},
                                            .properties = {}});

  CHECK_FALSE(connack.empty());
  CHECK_FALSE(pingresp.empty());
  CHECK_FALSE(disconnect.empty());
  CHECK_FALSE(auth.empty());
  CHECK_FALSE(suback.empty());
  CHECK_FALSE(unsuback.empty());

  StreamBuffer stream_buffer;
  stream_buffer.append(pingresp);
  const std::optional<AnyPacket> packet = try_decode_packet(stream_buffer);
  REQUIRE(packet.has_value());
  CHECK(std::holds_alternative<PingrespPacket>(*packet));
}

TEST_CASE("connection_flow_support_stop_and_timeout_helpers", "[connection]") {
  TcpConnection connection(k_invalid_socket);
  bool stopped = false;

  CHECK_FALSE(send_connack_and_stop(connection, nullptr, false,
                                    [&stopped]() { stopped = true; },
                                    ReasonCode::ProtocolError));
  CHECK(stopped);

  stopped = false;
  CHECK_FALSE(send_v311_reject_and_stop(connection, nullptr, false,
                                        [&stopped]() { stopped = true; }));
  CHECK(stopped);

  RuntimeDisconnectState disconnect_state;
  ConnectPacket connect_packet;
  write_error_disconnect(connection, nullptr, false, connect_packet,
                         disconnect_state, ReasonCode::ProtocolError,
                         "err");
  CHECK(disconnect_state.clean_disconnect);
  CHECK(disconnect_state.reason_code == ReasonCode::ProtocolError);

  CHECK_NOTHROW(set_receive_timeout(connection, nullptr, 100U));
}
