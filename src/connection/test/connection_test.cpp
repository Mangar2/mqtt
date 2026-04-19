#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "connection/connection_error.h"
#include "connection/outbound_queue_bridge.h"
#include "connection/connection_state.h"
#include "connection/keep_alive_timer.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "connection/client_handler.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/tcp_listener.h"
#include "outbound_queue/outbound_queue.h"
#include "transport/websocket_frame_codec.h"

namespace mqtt {

namespace {

struct SocketPair {
  SocketHandle client_fd{k_invalid_socket};
  std::unique_ptr<TcpConnection> server_connection;
};

SocketPair make_socket_pair() {
  auto listener = TcpListener::listen(0U, false);
  const uint16_t bound_port = listener.port();

  std::unique_ptr<TcpConnection> accepted_connection;
  std::thread accept_thread([
      &listener, &accepted_connection] {
    accepted_connection = listener.accept();
  });

  const SocketHandle client_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_socket != k_invalid_socket);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(bound_port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const int connect_result =
      ::connect(client_socket, reinterpret_cast<const sockaddr *>(&address),
                static_cast<int>(sizeof(address)));
  REQUIRE(connect_result == 0);

  accept_thread.join();
  REQUIRE(accepted_connection != nullptr);

  return {.client_fd = client_socket,
          .server_connection = std::move(accepted_connection)};
}

void close_socket_handle(SocketHandle socket_handle) {
  if (socket_handle == k_invalid_socket) {
    return;
  }
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

void set_recv_timeout(SocketHandle socket_handle, uint32_t timeout_millis) {
#ifdef _WIN32
  DWORD timeout_value = static_cast<DWORD>(timeout_millis);
  const int result = ::setsockopt(
      static_cast<SOCKET>(socket_handle), SOL_SOCKET, SO_RCVTIMEO,
      reinterpret_cast<const char *>(&timeout_value),
      static_cast<int>(sizeof(timeout_value)));
#else
  struct timeval timeout_value {};
  timeout_value.tv_sec = static_cast<time_t>(timeout_millis / 1000U);
  timeout_value.tv_usec =
      static_cast<suseconds_t>((timeout_millis % 1000U) * 1000U);
  const int result = ::setsockopt(
      static_cast<int>(socket_handle), SOL_SOCKET, SO_RCVTIMEO,
      &timeout_value, static_cast<socklen_t>(sizeof(timeout_value)));
#endif
  REQUIRE(result == 0);
}

void write_all(SocketHandle socket_handle, const std::vector<uint8_t> &bytes) {
  std::size_t sent_total = 0U;
  while (sent_total < bytes.size()) {
#ifdef _WIN32
    const int sent_now =
        ::send(static_cast<SOCKET>(socket_handle),
               reinterpret_cast<const char *>(bytes.data() + sent_total),
               static_cast<int>(bytes.size() - sent_total), 0);
#else
    const ssize_t sent_now =
        ::send(static_cast<int>(socket_handle), bytes.data() + sent_total,
               bytes.size() - sent_total, 0);
#endif
    REQUIRE(sent_now > 0);
    sent_total += static_cast<std::size_t>(sent_now);
  }
}

std::vector<uint8_t> read_some(SocketHandle socket_handle,
                               std::size_t max_bytes) {
  std::vector<uint8_t> buffer(max_bytes, 0U);
#ifdef _WIN32
  const int received =
      ::recv(static_cast<SOCKET>(socket_handle),
             reinterpret_cast<char *>(buffer.data()),
             static_cast<int>(buffer.size()), 0);
#else
  const ssize_t received =
      ::recv(static_cast<int>(socket_handle), buffer.data(), buffer.size(), 0);
#endif
  if (received <= 0) {
    return {};
  }
  buffer.resize(static_cast<std::size_t>(received));
  return buffer;
}

std::vector<uint8_t> read_nonempty_with_retries(SocketHandle socket_handle,
                                                std::size_t max_bytes,
                                                int attempts) {
  for (int attempt_idx = 0; attempt_idx < attempts; ++attempt_idx) {
    std::vector<uint8_t> data = read_some(socket_handle, max_bytes);
    if (!data.empty()) {
      return data;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return {};
}

BrokerConfig make_handler_test_config() {
  BrokerConfig config;
  config.mqtt_port = 0U;
  config.ws_port = 0U;
  config.allow_anonymous = true;
  config.persistence_enabled = false;
  config.tick_interval_ms = 100U;
  return config;
}

WriteBuffer make_connect_frame(std::string client_id, uint16_t keep_alive) {
  ConnectPacket packet;
  packet.client_id.value = std::move(client_id);
  packet.keep_alive = keep_alive;
  packet.clean_start = true;
  WriteBuffer frame;
  encode_connect(frame, packet);
  return frame;
}

WriteBuffer make_connect_with_flags_frame(std::string client_id,
                                          uint16_t keep_alive,
                                          std::optional<uint8_t> request_problem_information,
                                          std::optional<uint32_t> session_expiry_interval,
                                          bool clean_start = true) {
  ConnectPacket packet;
  packet.client_id.value = std::move(client_id);
  packet.keep_alive = keep_alive;
  packet.clean_start = clean_start;

  if (request_problem_information.has_value()) {
    packet.properties.push_back(
        Property{.id = PropertyId::RequestProblemInformation,
                 .value = *request_problem_information});
  }

  if (session_expiry_interval.has_value()) {
    packet.properties.push_back(
        Property{.id = PropertyId::SessionExpiryInterval,
                 .value = *session_expiry_interval});
  }

  WriteBuffer frame;
  encode_connect(frame, packet);
  return frame;
}

WriteBuffer make_connect_with_credentials_frame(std::string client_id,
                                                std::string username,
                                                const std::string &password,
                                                uint16_t keep_alive = 10U) {
  ConnectPacket packet;
  packet.client_id.value = std::move(client_id);
  packet.keep_alive = keep_alive;
  packet.clean_start = true;
  packet.username = Utf8String{std::move(username)};
  BinaryData pwd_data;
  pwd_data.data.reserve(password.size());
  for (char chr : password) {
    pwd_data.data.push_back(static_cast<uint8_t>(chr));
  }
  packet.password = std::move(pwd_data);
  WriteBuffer frame;
  encode_connect(frame, packet);
  return frame;
}

WriteBuffer make_enhanced_connect_frame(std::string client_id,
                                        std::string auth_method) {
  ConnectPacket packet;
  packet.client_id.value = std::move(client_id);
  packet.keep_alive = 10U;
  packet.clean_start = true;
  packet.properties.push_back(
      Property{.id = PropertyId::AuthenticationMethod,
               .value = Utf8String{std::move(auth_method)}});
  WriteBuffer frame;
  encode_connect(frame, packet);
  return frame;
}

WriteBuffer make_pingreq_frame() {
  WriteBuffer frame;
  encode_pingreq(frame);
  return frame;
}

WriteBuffer make_disconnect_frame(ReasonCode reason_code = ReasonCode::Success) {
  WriteBuffer frame;
  encode_disconnect(
      frame, DisconnectPacket{.reason_code = reason_code, .properties = {}});
  return frame;
}

WriteBuffer make_connack_frame(ReasonCode reason_code = ReasonCode::Success) {
  WriteBuffer frame;
  encode_connack(frame,
                 ConnackPacket{.session_present = false,
                               .reason_code = reason_code,
                               .properties = {}});
  return frame;
}

WriteBuffer make_suback_frame(uint16_t packet_id = 1U) {
  WriteBuffer frame;
  encode_suback(frame, SubackPacket{.packet_id = packet_id,
                                    .properties = {},
                                    .reason_codes = {ReasonCode::Success}});
  return frame;
}

WriteBuffer make_unsuback_frame(uint16_t packet_id = 1U) {
  WriteBuffer frame;
  encode_unsuback(frame,
                  UnsubackPacket{.packet_id = packet_id,
                                 .properties = {},
                                 .reason_codes = {ReasonCode::Success}});
  return frame;
}

WriteBuffer make_disconnect_with_expiry_frame(uint32_t expiry_value) {
  WriteBuffer frame;
  encode_disconnect(
      frame,
      DisconnectPacket{
          .reason_code = ReasonCode::Success,
          .properties = {Property{.id = PropertyId::UserProperty,
                                  .value = Utf8StringPair{.name = Utf8String{"k"},
                                                          .value = Utf8String{"v"}}},
                         Property{.id = PropertyId::SessionExpiryInterval,
                                  .value = FourByteInteger{expiry_value}}}});
  return frame;
}

DisconnectPacket decode_disconnect_packet_or_fail(const std::vector<uint8_t> &frame) {
  REQUIRE_FALSE(frame.empty());
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  REQUIRE(std::holds_alternative<DisconnectPacket>(packet));
  return std::get<DisconnectPacket>(packet);
}

ConnackPacket decode_connack_packet_or_fail(const std::vector<uint8_t> &frame) {
  REQUIRE_FALSE(frame.empty());
  ReadBuffer read_buffer(frame);
  const AnyPacket packet = read_packet(read_buffer);
  REQUIRE(std::holds_alternative<ConnackPacket>(packet));
  return std::get<ConnackPacket>(packet);
}

bool has_reason_string_property(const DisconnectPacket &packet) {
  return std::ranges::any_of(packet.properties,
                             [](const Property &property) {
                               return property.id ==
                                      PropertyId::ReasonString;
                             });
}

WriteBuffer make_auth_frame(std::string auth_method,
                            const std::string &auth_payload,
                            ReasonCode reason_code =
                                ReasonCode::ContinueAuthentication) {
  AuthPacket packet;
  packet.reason_code = reason_code;
  packet.properties.push_back(
      Property{.id = PropertyId::AuthenticationMethod,
               .value = Utf8String{std::move(auth_method)}});
  BinaryData payload;
  payload.data.reserve(auth_payload.size());
  for (char chr : auth_payload) {
    payload.data.push_back(static_cast<uint8_t>(chr));
  }
  packet.properties.push_back(
      Property{.id = PropertyId::AuthenticationData, .value = std::move(payload)});
  WriteBuffer frame;
  encode_auth(frame, packet);
  return frame;
}

WriteBuffer make_subscribe_frame() {
  SubscribePacket packet;
  packet.packet_id = 1U;
  packet.filters.push_back({.topic_filter = Utf8String{"sensors/temp"},
                            .options = SubscribeOptions{.max_qos =
                                                            QoS::AtMostOnce,
                                                        .no_local = false,
                                                        .retain_as_published =
                                                            false,
                                                        .retain_handling =
                                                            0U}});
  WriteBuffer frame;
  encode_subscribe(frame, packet);
  return frame;
}

WriteBuffer make_subscribe_qos2_frame(std::string topic_name) {
  SubscribePacket packet;
  packet.packet_id = 3U;
  packet.filters.push_back(
      {.topic_filter = Utf8String{std::move(topic_name)},
       .options = SubscribeOptions{.max_qos = QoS::ExactlyOnce,
                                   .no_local = false,
                                   .retain_as_published = false,
                                   .retain_handling = 0U}});
  WriteBuffer frame;
  encode_subscribe(frame, packet);
  return frame;
}

WriteBuffer make_unsubscribe_frame() {
  UnsubscribePacket packet;
  packet.packet_id = 2U;
  packet.topic_filters.push_back(Utf8String{"sensors/temp"});
  WriteBuffer frame;
  encode_unsubscribe(frame, packet);
  return frame;
}

WriteBuffer make_publish_qos0_frame() {
  PublishPacket packet;
  packet.qos = QoS::AtMostOnce;
  packet.topic.value = "sensors/temp";
  packet.payload.data = {0x31U, 0x32U};
  WriteBuffer frame;
  encode_publish(frame, packet);
  return frame;
}

WriteBuffer make_publish_qos2_frame(uint16_t packet_id) {
  PublishPacket packet;
  packet.qos = QoS::ExactlyOnce;
  packet.topic.value = "sensors/qos2";
  packet.packet_id = packet_id;
  packet.payload.data = {0x41U};
  WriteBuffer frame;
  encode_publish(frame, packet);
  return frame;
}

WriteBuffer make_pubrel_frame(uint16_t packet_id) {
  WriteBuffer frame;
  encode_pubrel(frame, PubrelPacket{.packet_id = packet_id,
                                    .reason_code = ReasonCode::Success,
                                    .properties = {}});
  return frame;
}

WriteBuffer make_pubrec_frame(uint16_t packet_id) {
  WriteBuffer frame;
  encode_pubrec(frame,
                PubrecPacket{.packet_id = packet_id,
                             .reason_code = ReasonCode::Success,
                             .properties = {}});
  return frame;
}

std::optional<uint16_t>
decode_publish_packet_id(const std::vector<uint8_t> &frame) {
  if (frame.empty() || static_cast<uint8_t>(frame[0] >> 4U) != 3U) {
    return std::nullopt;
  }

  std::size_t index = 1U;
  std::size_t multiplier = 1U;
  std::size_t remaining_length = 0U;
  while (index < frame.size()) {
    const uint8_t encoded = frame[index];
    remaining_length += static_cast<std::size_t>(encoded & 0x7FU) * multiplier;
    ++index;
    if ((encoded & 0x80U) == 0U) {
      break;
    }
    multiplier *= 128U;
    if (multiplier > 128U * 128U * 128U * 128U) {
      return std::nullopt;
    }
  }

  if (index + remaining_length > frame.size()) {
    return std::nullopt;
  }
  if (index + 2U > frame.size()) {
    return std::nullopt;
  }

  const std::size_t topic_size =
      (static_cast<std::size_t>(frame[index]) << 8U) |
      static_cast<std::size_t>(frame[index + 1U]);
  index += 2U;

  if (index + topic_size + 2U > frame.size()) {
    return std::nullopt;
  }

  index += topic_size;
  const auto packet_id =
      static_cast<uint16_t>((static_cast<uint16_t>(frame[index]) << 8U) |
                            static_cast<uint16_t>(frame[index + 1U]));
  if (packet_id == 0U) {
    return std::nullopt;
  }
  return packet_id;
}

std::vector<uint8_t> malformed_connect_frame() {
  return {0x10U, 0x01U, 0x00U};
}

std::vector<uint8_t> malformed_runtime_frame() {
  return {0x00U, 0x00U};
}

std::vector<uint8_t> make_valid_upgrade_request() {
  const std::string request =
      "GET /mqtt HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  return {request.begin(), request.end()};
}

std::vector<uint8_t> make_invalid_upgrade_request() {
  const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  return {request.begin(), request.end()};
}

std::vector<uint8_t> make_masked_ws_binary_frame(
    const std::vector<uint8_t> &payload,
    std::array<uint8_t, 4> mask = {0x12U, 0x34U, 0x56U, 0x78U}) {
  std::vector<uint8_t> frame;
  frame.push_back(0x82U); // FIN + binary opcode

  const std::size_t payload_size = payload.size();
  if (payload_size <= 125U) {
    frame.push_back(static_cast<uint8_t>(0x80U | payload_size));
  } else if (payload_size <= 65535U) {
    frame.push_back(0x80U | 126U);
    frame.push_back(static_cast<uint8_t>((payload_size >> 8U) & 0xFFU));
    frame.push_back(static_cast<uint8_t>(payload_size & 0xFFU));
  } else {
    frame.push_back(0x80U | 127U);
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<uint8_t>((payload_size >> shift) & 0xFFU));
    }
  }

  frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t idx = 0U; idx < payload_size; ++idx) {
    frame.push_back(payload[idx] ^ mask[idx % 4U]);
  }
  return frame;
}

std::vector<uint8_t> decode_first_ws_payload(const std::vector<uint8_t> &frame) {
  WebSocketFrameCodec codec;
  codec.append(frame);
  if (!codec.has_frame()) {
    return {};
  }
  return codec.consume_frame().payload;
}

uint8_t packet_type_nibble(const std::vector<uint8_t> &frame) {
  if (frame.empty()) {
    return 0xFFU;
  }
  return static_cast<uint8_t>(frame[0] >> 4U);
}

} // namespace

//  ConnectionError codec mapping
//
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("map_codec_error_to_connect_reason_handles_all_key_paths",
      "[connection]") {
  CHECK(map_codec_error_to_connect_reason(CodecError::InvalidProtocolVersion) ==
    ReasonCode::UnsupportedProtocolVersion);
  CHECK(map_codec_error_to_connect_reason(CodecError::BufferTooShort) ==
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
  CHECK(map_codec_error_to_connect_reason(
    static_cast<CodecError>(0xFFU)) == ReasonCode::ProtocolError);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("map_codec_error_to_runtime_reason_handles_all_key_paths",
      "[connection]") {
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
  CHECK(map_codec_error_to_runtime_reason(CodecError::DuplicateProperty) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::PropertyNotAllowed) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidProtocolVersion) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(CodecError::InvalidQoS) ==
    ReasonCode::MalformedPacket);
  CHECK(map_codec_error_to_runtime_reason(
    static_cast<CodecError>(0xFFU)) == ReasonCode::ProtocolError);
}

//  ConnectionStateMachine
//

TEST_CASE("state_initial_is_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  CHECK(fsm.state() == ConnectionState::Connecting);
}

TEST_CASE("on_connect_transitions_to_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK(fsm.state() == ConnectionState::Connected);
}

TEST_CASE("on_connect_throws_duplicate", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::DuplicateConnect);
  }
}

TEST_CASE("on_connect_throws_in_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_connect_throws_in_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.close();
  try {
    fsm.on_connect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_disconnect_transitions_to_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  CHECK(fsm.state() == ConnectionState::Disconnecting);
}

TEST_CASE("on_disconnect_throws_if_not_connected", "[connection]") {
  ConnectionStateMachine fsm;
  try {
    fsm.on_disconnect();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("on_connection_lost_transitions_to_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_connection_lost();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("on_connection_lost_from_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connection_lost();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("close_transitions_to_closed", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.close();
  CHECK(fsm.state() == ConnectionState::Closed);
}

TEST_CASE("is_connected_true_when_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK(fsm.is_connected());
}

TEST_CASE("is_connected_false_when_not_connected", "[connection]") {
  ConnectionStateMachine fsm;
  CHECK_FALSE(fsm.is_connected());
}

TEST_CASE("enforce_not_connecting_passes_in_connected", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  CHECK_NOTHROW(fsm.enforce_not_connecting());
}

TEST_CASE("enforce_not_connecting_throws_in_connecting", "[connection]") {
  ConnectionStateMachine fsm;
  try {
    fsm.enforce_not_connecting();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::ConnectRequired);
  }
}

TEST_CASE("enforce_not_connecting_throws_in_disconnecting", "[connection]") {
  ConnectionStateMachine fsm;
  fsm.on_connect();
  fsm.on_disconnect();
  try {
    fsm.enforce_not_connecting();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

//  KeepAliveTimer
//

TEST_CASE("disabled_when_keep_alive_is_zero", "[connection]") {
  KeepAliveTimer timer(0U);
  CHECK_FALSE(timer.is_enabled());
}

TEST_CASE("not_expired_immediately_after_construction", "[connection]") {
  KeepAliveTimer timer(60U);
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("not_expired_after_reset", "[connection]") {
  KeepAliveTimer timer(60U);
  timer.reset();
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("disabled_timer_never_expires", "[connection]") {
  KeepAliveTimer timer(0U);
  CHECK_FALSE(timer.is_expired());
  timer.reset();
  CHECK_FALSE(timer.is_expired());
}

TEST_CASE("enabled_when_keep_alive_nonzero", "[connection]") {
  KeepAliveTimer timer(10U);
  CHECK(timer.is_enabled());
}

//  TopicAliasTable
//

TEST_CASE("max_aliases_returns_configured_value", "[connection]") {
  TopicAliasTable tab(10U);
  CHECK(tab.max_aliases() == 10U);
}

TEST_CASE("set_and_get_inbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a/b");
  CHECK(tab.get_inbound(1U) == "a/b");
}

TEST_CASE("set_and_get_outbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_outbound("a/b", 1U);
  CHECK(tab.get_outbound("a/b") == 1U);
}

TEST_CASE("get_outbound_unknown_returns_nullopt", "[connection]") {
  TopicAliasTable tab(10U);
  CHECK(tab.get_outbound("x") == std::nullopt);
}

TEST_CASE("get_inbound_unknown_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    (void)tab.get_inbound(1U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasNotFound);
  }
}

TEST_CASE("set_inbound_alias_zero_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    tab.set_inbound(0U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_inbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    tab.set_inbound(6U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_outbound_alias_zero_throws", "[connection]") {
  TopicAliasTable tab(10U);
  try {
    tab.set_outbound("a", 0U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("set_outbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    tab.set_outbound("a", 6U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("get_inbound_alias_exceeds_max_throws", "[connection]") {
  TopicAliasTable tab(5U);
  try {
    (void)tab.get_inbound(6U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

TEST_CASE("reset_clears_inbound_mappings", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a");
  tab.reset();
  try {
    (void)tab.get_inbound(1U);
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasNotFound);
  }
}

TEST_CASE("reset_clears_outbound_mappings", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_outbound("a", 1U);
  tab.reset();
  CHECK(tab.get_outbound("a") == std::nullopt);
}

TEST_CASE("overwrite_inbound_alias", "[connection]") {
  TopicAliasTable tab(10U);
  tab.set_inbound(1U, "a");
  tab.set_inbound(1U, "b");
  CHECK(tab.get_inbound(1U) == "b");
}

TEST_CASE("max_alias_zero_disables_inbound", "[connection]") {
  TopicAliasTable tab(0U);
  try {
    tab.set_inbound(1U, "a");
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::AliasOutOfRange);
  }
}

//  ReceiveMaximum
//

TEST_CASE("max_returns_configured_value", "[connection]") {
  ReceiveMaximum receive_maximum(10U);
  CHECK(receive_maximum.max() == 10U);
}

TEST_CASE("zero_max_defaults_to_65535", "[connection]") {
  ReceiveMaximum receive_maximum(0U);
  CHECK(receive_maximum.max() == 65535U);
}

TEST_CASE("acquire_succeeds_within_limit", "[connection]") {
  ReceiveMaximum receive_maximum(5U);
  CHECK(receive_maximum.acquire());
}

TEST_CASE("acquire_returns_false_at_limit", "[connection]") {
  ReceiveMaximum receive_maximum(2U);
  CHECK(receive_maximum.acquire());
  CHECK(receive_maximum.acquire());
  CHECK_FALSE(receive_maximum.acquire());
}

TEST_CASE("is_paused_true_when_limit_reached", "[connection]") {
  ReceiveMaximum receive_maximum(1U);
  (void)receive_maximum.acquire();
  CHECK(receive_maximum.is_paused());
}

TEST_CASE("is_paused_false_initially", "[connection]") {
  ReceiveMaximum receive_maximum(10U);
  CHECK_FALSE(receive_maximum.is_paused());
}

TEST_CASE("available_decreases_after_acquire", "[connection]") {
  ReceiveMaximum receive_maximum(5U);
  (void)receive_maximum.acquire();
  (void)receive_maximum.acquire();
  CHECK(receive_maximum.available() == 3U);
}

TEST_CASE("available_increases_after_release", "[connection]") {
  ReceiveMaximum receive_maximum(2U);
  (void)receive_maximum.acquire();
  (void)receive_maximum.acquire();
  receive_maximum.release();
  CHECK(receive_maximum.available() == 1U);
}

TEST_CASE("release_restores_capacity", "[connection]") {
  ReceiveMaximum receive_maximum(1U);
  (void)receive_maximum.acquire();
  receive_maximum.release();
  CHECK(receive_maximum.acquire());
}

TEST_CASE("release_throws_when_inflight_zero", "[connection]") {
  ReceiveMaximum receive_maximum(5U);
  try {
    receive_maximum.release();
    FAIL("Expected ConnectionException");
  } catch (const ConnectionException &exc) {
    CHECK(exc.error() == ConnectionError::InvalidState);
  }
}

TEST_CASE("drain_pending_outbound_messages_returns_fifo_and_empties_source",
          "[connection]") {
  OutboundQueue source_queue;

  Message first_message;
  first_message.topic.value = "topic/one";
  Message second_message;
  second_message.topic.value = "topic/two";
  Message third_message;
  third_message.topic.value = "topic/three";

  REQUIRE(source_queue.push(first_message));
  REQUIRE(source_queue.push(second_message));
  REQUIRE(source_queue.push(third_message));

  std::vector<Message> drained_messages =
      drain_pending_outbound_messages(source_queue);

  REQUIRE(drained_messages.size() == 3U);
  CHECK(drained_messages[0].topic.value == "topic/one");
  CHECK(drained_messages[1].topic.value == "topic/two");
  CHECK(drained_messages[2].topic.value == "topic/three");
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

//  ClientHandler (Module 24)
//

TEST_CASE("client_handler_run_with_connection_pointer", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  ClientHandler handler;

  auto conn = std::make_unique<TcpConnection>(k_invalid_socket);
  CHECK_NOTHROW(handler.run(std::move(conn), broker, cfg, false));
}

TEST_CASE("client_handler_run_with_null_connection", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  ClientHandler handler;

  std::unique_ptr<TcpConnection> conn;
  CHECK_NOTHROW(handler.run(std::move(conn), broker, cfg, true));
}

TEST_CASE("client_handler_connect_ping_disconnect_roundtrip", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 3000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-1", 10U));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd, make_pingreq_frame());
  const std::vector<uint8_t> pingresp = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(pingresp.empty());
  CHECK(packet_type_nibble(pingresp) == 13U);

  write_all(sockets.client_fd, make_disconnect_frame());
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_connect_timeout_then_partial_connect_succeeds",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 3000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(180));

  const WriteBuffer connect_frame = make_connect_frame("client-partial", 10U);
  REQUIRE(connect_frame.size() > 2U);
  std::vector<uint8_t> first_part(connect_frame.begin(), connect_frame.begin() + 2);
  std::vector<uint8_t> second_part(connect_frame.begin() + 2, connect_frame.end());
  write_all(sockets.client_fd, first_part);
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  write_all(sockets.client_fd, second_part);

  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd, make_disconnect_frame());
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_subscribe_unsubscribe_and_publish", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-2", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_subscribe_frame());
  const std::vector<uint8_t> suback = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(suback.empty());
  CHECK(packet_type_nibble(suback) == 9U);

  write_all(sockets.client_fd, make_publish_qos0_frame());
  // QoS 0 publishes do not produce immediate ACK frames.
  (void)read_some(sockets.client_fd, 64U);

  write_all(sockets.client_fd, make_unsubscribe_frame());
  const std::vector<uint8_t> unsuback = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(unsuback.empty());
  CHECK(packet_type_nibble(unsuback) == 11U);

  write_all(sockets.client_fd, make_disconnect_frame());
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_rejects_non_connect_first_packet", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_pingreq_frame());
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_keep_alive_timeout_emits_disconnect", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.tick_interval_ms = 100U;

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 3000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-3", 1U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_server_keep_alive_override_emits_disconnect",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.tick_interval_ms = 100U;
  cfg.server_keep_alive = 1U;

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 3000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-3-override", 60U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_connect_malformed_packet_returns_malformed_packet_connack",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, malformed_connect_frame());
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  const ConnackPacket connack_packet = decode_connack_packet_or_fail(connack);
  CHECK(connack_packet.reason_code == ReasonCode::MalformedPacket);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_runtime_malformed_packet_returns_disconnect",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-4", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, malformed_runtime_frame());
  const std::vector<uint8_t> disconnect =
      read_nonempty_with_retries(sockets.client_fd, 512U, 6);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  const DisconnectPacket disconnect_packet = decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::MalformedPacket);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_abrupt_socket_close_is_connection_lost_path",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-5", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_disconnect_with_expiry_override", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-6", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_disconnect_with_expiry_frame(42U));
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_second_connect_triggers_protocol_disconnect",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-8", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_connect_frame("client-8", 10U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_second_connect_with_problem_information_includes_reason_string",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd,
            make_connect_with_flags_frame("client-8b", 10U, 1U, std::nullopt));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_connect_frame("client-8b", 10U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  const DisconnectPacket disconnect_packet = decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::ProtocolError);
  CHECK(has_reason_string_property(disconnect_packet));

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_second_connect_with_problem_information_off_omits_reason_string",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd,
            make_connect_with_flags_frame("client-8c", 10U, 0U, std::nullopt));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_connect_frame("client-8c", 10U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  const DisconnectPacket disconnect_packet = decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::ProtocolError);
  CHECK_FALSE(has_reason_string_property(disconnect_packet));

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_disconnect_expiry_increase_from_zero_returns_protocol_error",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(
      sockets.client_fd,
      make_connect_with_flags_frame("client-6b", 10U, 1U, 0U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_disconnect_with_expiry_frame(42U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  const DisconnectPacket disconnect_packet = decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::ProtocolError);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_pingresp_packet_hits_default_protocol_error",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-9", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  WriteBuffer pingresp_frame;
  encode_pingresp(pingresp_frame);
  write_all(sockets.client_fd, pingresp_frame);

  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_runtime_connack_packet_hits_default_protocol_error",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-10", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_connack_frame(ReasonCode::Success));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_runtime_suback_packet_hits_default_protocol_error",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-10-suback", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_suback_frame(55U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_runtime_unsuback_packet_hits_default_protocol_error",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-10-unsuback", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_unsuback_frame(56U));
  const std::vector<uint8_t> disconnect = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_websocket_upgrade_connect_and_disconnect",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, true);
  });

  write_all(sockets.client_fd, make_valid_upgrade_request());
  const std::vector<uint8_t> handshake_response = read_some(sockets.client_fd, 2048U);
  CHECK_FALSE(handshake_response.empty());

  const WriteBuffer connect_frame = make_connect_frame("ws-client", 10U);
  write_all(sockets.client_fd, make_masked_ws_binary_frame(connect_frame));

  const std::vector<uint8_t> ws_connack =
      read_nonempty_with_retries(sockets.client_fd, 2048U, 8);
  const std::vector<uint8_t> connack_payload = decode_first_ws_payload(ws_connack);
  CHECK_FALSE(connack_payload.empty());
  CHECK(packet_type_nibble(connack_payload) == 2U);

  const WriteBuffer disconnect_frame = make_disconnect_frame();
  write_all(sockets.client_fd, make_masked_ws_binary_frame(disconnect_frame));

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_websocket_invalid_upgrade_closes_connection",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, true);
  });

  write_all(sockets.client_fd, make_invalid_upgrade_request());
  const std::vector<uint8_t> response = read_some(sockets.client_fd, 1024U);
  CHECK(response.empty());

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_websocket_rejects_non_connect_first_packet",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, true);
  });

  write_all(sockets.client_fd, make_valid_upgrade_request());
  const std::vector<uint8_t> handshake_response =
      read_some(sockets.client_fd, 2048U);
  CHECK_FALSE(handshake_response.empty());

  const WriteBuffer pingreq_frame = make_pingreq_frame();
  write_all(sockets.client_fd, make_masked_ws_binary_frame(pingreq_frame));

  const std::vector<uint8_t> ws_connack =
      read_nonempty_with_retries(sockets.client_fd, 2048U, 8);
  const std::vector<uint8_t> connack_payload = decode_first_ws_payload(ws_connack);
  CHECK_FALSE(connack_payload.empty());
  CHECK(packet_type_nibble(connack_payload) == 2U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_enhanced_auth_connect_flow", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {
      {.username = "auth-user", .password = "auth-pass"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_enhanced_connect_frame("auth-client", "PLAIN"));
  const std::vector<uint8_t> auth_challenge = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(auth_challenge.empty());
  CHECK(packet_type_nibble(auth_challenge) == 15U);

  write_all(sockets.client_fd,
            make_auth_frame("PLAIN", "auth-user:auth-pass",
                            ReasonCode::ContinueAuthentication));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd, make_disconnect_frame());
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_enhanced_auth_timeout_then_close_aborts_handshake",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {
      {.username = "auth-user", .password = "auth-pass"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_enhanced_connect_frame("auth-timeout", "PLAIN"));
  const std::vector<uint8_t> auth_challenge = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(auth_challenge.empty());
  CHECK(packet_type_nibble(auth_challenge) == 15U);

  std::this_thread::sleep_for(std::chrono::milliseconds(180));
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_inbound_qos2_publish_rel_flow", "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-7", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_publish_qos2_frame(7U));
  const std::vector<uint8_t> pubrec = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(pubrec.empty());
  CHECK(packet_type_nibble(pubrec) == 5U);

  write_all(sockets.client_fd, make_pubrel_frame(7U));
  const std::vector<uint8_t> pubcomp = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(pubcomp.empty());
  CHECK(packet_type_nibble(pubcomp) == 7U);

  write_all(sockets.client_fd, make_disconnect_frame());
  close_socket_handle(sockets.client_fd);

  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_connect_auth_failure_returns_error_connack",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u1", .password = "p1"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd,
            make_connect_with_credentials_frame("auth-fail", "u1", "bad"));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_enhanced_auth_wrong_packet_fails_handshake",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u2", .password = "p2"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_enhanced_connect_frame("auth-wrong", "PLAIN"));
  const std::vector<uint8_t> auth_packet = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(auth_packet.empty());
  CHECK(packet_type_nibble(auth_packet) == 15U);

  write_all(sockets.client_fd, make_pingreq_frame());
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_enhanced_auth_malformed_packet_fails_handshake",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u2", .password = "p2"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_enhanced_connect_frame("auth-malformed", "PLAIN"));
  const std::vector<uint8_t> auth_packet = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(auth_packet.empty());
  CHECK(packet_type_nibble(auth_packet) == 15U);

  write_all(sockets.client_fd, malformed_runtime_frame());
  const std::vector<uint8_t> connack = read_nonempty_with_retries(sockets.client_fd, 512U, 8);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  const ConnackPacket connack_packet = decode_connack_packet_or_fail(connack);
  CHECK(connack_packet.reason_code == ReasonCode::MalformedPacket);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_persistent_reconnect_sets_session_present",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  {
    SocketPair first_connection = make_socket_pair();
    set_recv_timeout(first_connection.client_fd, 1500U);

    ClientHandler first_handler;
    std::thread first_worker([&first_handler, &broker, &cfg, server_connection = std::move(first_connection.server_connection)]() mutable {
      first_handler.run(std::move(server_connection), broker, cfg, false);
    });

    write_all(first_connection.client_fd,
              make_connect_with_flags_frame("persistent-client", 10U,
                                            std::nullopt, 120U, false));
    const std::vector<uint8_t> first_connack =
        read_some(first_connection.client_fd, 512U);
    CHECK_FALSE(first_connack.empty());
    CHECK(packet_type_nibble(first_connack) == 2U);

    write_all(first_connection.client_fd,
              make_disconnect_with_expiry_frame(120U));
    close_socket_handle(first_connection.client_fd);
    first_worker.join();
  }

  {
    SocketPair second_connection = make_socket_pair();
    set_recv_timeout(second_connection.client_fd, 1500U);

    ClientHandler second_handler;
    std::thread second_worker([&second_handler, &broker, &cfg, server_connection = std::move(second_connection.server_connection)]() mutable {
      second_handler.run(std::move(server_connection), broker, cfg, false);
    });

    write_all(second_connection.client_fd,
              make_connect_with_flags_frame("persistent-client", 10U,
                                            std::nullopt, 120U, false));
    const std::vector<uint8_t> second_connack =
        read_some(second_connection.client_fd, 512U);
    CHECK_FALSE(second_connack.empty());
    CHECK(packet_type_nibble(second_connack) == 2U);

    const ConnackPacket connack_packet = decode_connack_packet_or_fail(second_connack);
    CHECK(connack_packet.session_present);

    write_all(second_connection.client_fd, make_disconnect_frame());
    close_socket_handle(second_connection.client_fd);
    second_worker.join();
  }

  broker.shutdown();
}

TEST_CASE("client_handler_auth_packet_after_connect_handles_invalid_reauth_without_abort",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u3", .password = "p3"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_with_credentials_frame("auth-runtime",
                                                                    "u3", "p3"));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd,
            make_auth_frame("PLAIN", "u3:p3", ReasonCode::ReAuthenticate));
  const std::vector<uint8_t> disconnect =
      read_nonempty_with_retries(sockets.client_fd, 512U, 8);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_runtime_auth_failure_disconnects_cleanly",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u4", .password = "p4"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_enhanced_connect_frame("auth-runtime-failure", "PLAIN"));
  (void)read_some(sockets.client_fd, 512U); // AUTH challenge

  write_all(sockets.client_fd,
            make_auth_frame("PLAIN", "u4:p4",
                            ReasonCode::ContinueAuthentication));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd,
            [&]() {
              AuthPacket reauth_start;
              reauth_start.reason_code = ReasonCode::ReAuthenticate;
              reauth_start.properties.push_back(
                  Property{.id = PropertyId::AuthenticationMethod,
                           .value = Utf8String{"PLAIN"}});
              WriteBuffer frame;
              encode_auth(frame, reauth_start);
              return frame;
            }());
  const std::vector<uint8_t> reauth_challenge =
      read_nonempty_with_retries(sockets.client_fd, 512U, 8);
  CHECK_FALSE(reauth_challenge.empty());
  CHECK(packet_type_nibble(reauth_challenge) == 15U);

  write_all(sockets.client_fd,
            make_auth_frame("PLAIN", "wrong:credentials",
                            ReasonCode::ContinueAuthentication));
  const std::vector<uint8_t> disconnect =
      read_nonempty_with_retries(sockets.client_fd, 512U, 8);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();

  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);
  const DisconnectPacket disconnect_packet =
      decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::BadUserNameOrPassword);
}

TEST_CASE("client_handler_runtime_reauthenticate_failure_disconnects_cleanly",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {{.username = "u5", .password = "p5"}};

  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd,
            make_enhanced_connect_frame("auth-runtime-reauth", "PLAIN"));
  (void)read_some(sockets.client_fd, 512U); // AUTH challenge

  write_all(sockets.client_fd,
            make_auth_frame("PLAIN", "u5:p5",
                            ReasonCode::ContinueAuthentication));
  const std::vector<uint8_t> connack = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(connack.empty());
  CHECK(packet_type_nibble(connack) == 2U);

  write_all(sockets.client_fd,
        make_auth_frame("SCRAM", "wrong:credentials",
                            ReasonCode::ReAuthenticate));
  const std::vector<uint8_t> disconnect =
      read_nonempty_with_retries(sockets.client_fd, 512U, 8);
  CHECK_FALSE(disconnect.empty());
  CHECK(packet_type_nibble(disconnect) == 14U);
  const DisconnectPacket disconnect_packet =
      decode_disconnect_packet_or_fail(disconnect);
  CHECK(disconnect_packet.reason_code == ReasonCode::ProtocolError);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("client_handler_outbound_qos2_ack_flow_exercises_pubrec_and_pubcomp",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair sockets = make_socket_pair();
  set_recv_timeout(sockets.client_fd, 1500U);

  ClientHandler handler;
  std::thread worker([&handler, &broker, &cfg, server_connection = std::move(sockets.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(sockets.client_fd, make_connect_frame("client-qos2-out", 10U));
  (void)read_some(sockets.client_fd, 512U); // CONNACK

  write_all(sockets.client_fd, make_subscribe_qos2_frame("sensors/qos2"));
  const std::vector<uint8_t> suback = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(suback.empty());
  CHECK(packet_type_nibble(suback) == 9U);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  StreamBuffer response_stream;
  write_all(sockets.client_fd, make_publish_qos2_frame(21U));
  const std::vector<uint8_t> inbound_pubrec = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(inbound_pubrec.empty());
  response_stream.append(inbound_pubrec);
  bool seen_initial_pubrec = false;
  std::optional<uint16_t> outbound_packet_id;
  while (response_stream.has_complete_packet()) {
    const std::vector<uint8_t> frame = response_stream.consume_packet();
    const uint8_t packet_type = packet_type_nibble(frame);
    if (packet_type == 5U) {
      seen_initial_pubrec = true;
    }
    if (packet_type == 3U) {
      outbound_packet_id = decode_publish_packet_id(frame);
    }
  }
  CHECK(seen_initial_pubrec);

  write_all(sockets.client_fd, make_pubrel_frame(21U));
  bool seen_inbound_pubcomp = false;
  int empty_reads = 0;
  while (empty_reads < 20 &&
         (!seen_inbound_pubcomp || !outbound_packet_id.has_value())) {
    const std::vector<uint8_t> chunk = read_some(sockets.client_fd, 1024U);
    if (chunk.empty()) {
      ++empty_reads;
      continue;
    }

    response_stream.append(chunk);
    while (response_stream.has_complete_packet()) {
      const std::vector<uint8_t> frame = response_stream.consume_packet();
      const uint8_t packet_type = packet_type_nibble(frame);
      if (packet_type == 7U) {
        seen_inbound_pubcomp = true;
      }
      if (packet_type == 3U) {
        outbound_packet_id = decode_publish_packet_id(frame);
      }
    }
  }

  CHECK(seen_inbound_pubcomp);
  REQUIRE(outbound_packet_id.has_value());

  write_all(sockets.client_fd, make_pubrec_frame(*outbound_packet_id));
  const std::vector<uint8_t> outbound_pubrel = read_some(sockets.client_fd, 512U);
  CHECK_FALSE(outbound_pubrel.empty());
  CHECK(packet_type_nibble(outbound_pubrel) == 6U);

  close_socket_handle(sockets.client_fd);
  worker.join();
  broker.shutdown();
}

TEST_CASE("client_handler_session_takeover_executes_close_callback",
          "[connection]") {
  BrokerConfig cfg = make_handler_test_config();
  Broker broker(cfg);
  broker.startup();

  SocketPair first = make_socket_pair();
  SocketPair second = make_socket_pair();
  set_recv_timeout(first.client_fd, 2000U);
  set_recv_timeout(second.client_fd, 2000U);

  ClientHandler handler;
  std::thread worker_one([&handler, &broker, &cfg, server_connection = std::move(first.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });
  std::thread worker_two([&handler, &broker, &cfg, server_connection = std::move(second.server_connection)]() mutable {
    handler.run(std::move(server_connection), broker, cfg, false);
  });

  write_all(first.client_fd, make_connect_frame("same-client", 10U));
  const std::vector<uint8_t> first_connack = read_some(first.client_fd, 512U);
  CHECK_FALSE(first_connack.empty());

  write_all(second.client_fd, make_connect_frame("same-client", 10U));
  const std::vector<uint8_t> second_connack = read_some(second.client_fd, 512U);
  CHECK_FALSE(second_connack.empty());

  const std::vector<uint8_t> first_after_takeover =
      read_nonempty_with_retries(first.client_fd, 64U, 8);
  REQUIRE(first_after_takeover.size() >= 3U);
  CHECK(packet_type_nibble(first_after_takeover) == 14U);
  CHECK(first_after_takeover[2] ==
        static_cast<uint8_t>(ReasonCode::SessionTakenOver));

  write_all(second.client_fd, make_disconnect_frame());
  close_socket_handle(first.client_fd);
  close_socket_handle(second.client_fd);

  worker_one.join();
  worker_two.join();
  broker.shutdown();
}

} // namespace mqtt

