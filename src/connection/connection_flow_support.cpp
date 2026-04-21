/**
 * @file connection_flow_support.cpp
 * @brief Shared helpers for connection flow phases.
 */

#include "connection/connection_flow_support.h"

#include <array>
#include <cstddef>

#include "codec/codec_error.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "data_model/packet/packet_type.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/write_queue.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr std::size_t k_transport_read_chunk_size = 4096U;

[[nodiscard]] bool requests_problem_information(
    const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::RequestProblemInformation) {
      continue;
    }
    if (const auto *request_ptr = std::get_if<uint8_t>(&property.value)) {
      return *request_ptr != 0U;
    }
    return true;
  }
  return true;
}

} // namespace

WriteBuffer encode_connack_packet(const ConnackPacket &packet) {
  WriteBuffer frame;
  encode_connack(frame, packet);
  return frame;
}

WriteBuffer encode_suback_packet(const SubackPacket &packet) {
  WriteBuffer frame;
  encode_suback(frame, packet);
  return frame;
}

WriteBuffer encode_unsuback_packet(const UnsubackPacket &packet) {
  WriteBuffer frame;
  encode_unsuback(frame, packet);
  return frame;
}

WriteBuffer encode_pingresp_packet() {
  WriteBuffer frame;
  encode_pingresp(frame);
  return frame;
}

WriteBuffer encode_disconnect_packet(ReasonCode reason_code,
                                     const std::vector<Property> &properties) {
  WriteBuffer frame;
  encode_disconnect(frame,
                    DisconnectPacket{.reason_code = reason_code,
                                     .properties = properties});
  return frame;
}

WriteBuffer encode_auth_packet(ReasonCode reason_code,
                               const std::vector<Property> &properties) {
  WriteBuffer frame;
  encode_auth(frame,
              AuthPacket{.reason_code = reason_code, .properties = properties});
  return frame;
}

std::vector<Property> build_auth_properties(
    std::string_view auth_method, const std::optional<BinaryData> &auth_data,
    bool include_method) {
  std::vector<Property> properties;
  if (include_method && !auth_method.empty()) {
    properties.push_back(Property{.id = PropertyId::AuthenticationMethod,
                                  .value = Utf8String{std::string(auth_method)}});
  }
  if (auth_data.has_value()) {
    properties.push_back(
        Property{.id = PropertyId::AuthenticationData, .value = *auth_data});
  }
  return properties;
}

std::optional<uint32_t> find_session_expiry_override(
    const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::SessionExpiryInterval) {
      continue;
    }
    if (const auto *expiry_ptr = std::get_if<FourByteInteger>(&property.value)) {
      return *expiry_ptr;
    }
  }
  return std::nullopt;
}

std::optional<uint32_t> find_maximum_packet_size(
    const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::MaximumPacketSize) {
      continue;
    }
    if (const auto *size_ptr = std::get_if<FourByteInteger>(&property.value)) {
      return *size_ptr;
    }
  }
  return std::nullopt;
}

std::optional<uint16_t> find_receive_maximum(
    const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::ReceiveMaximum) {
      continue;
    }
    if (const auto *receive_maximum_ptr =
            std::get_if<TwoByteInteger>(&property.value)) {
      return *receive_maximum_ptr;
    }
  }
  return std::nullopt;
}

std::vector<Property> build_protocol_error_disconnect_properties(
    const ConnectPacket &connect_packet, std::string_view reason_text) {
  if (!requests_problem_information(connect_packet.properties)) {
    return {};
  }

  return {Property{.id = PropertyId::ReasonString,
                   .value = Utf8String{std::string(reason_text)}}};
}

bool enqueue_frame(WriteQueue &write_queue, WriteBuffer frame,
                   bool is_websocket) {
  if (is_websocket) {
    return write_queue.enqueue(WebSocketTransport::encode_frame(frame));
  }
  return write_queue.enqueue(std::move(frame));
}

void write_frame_direct(TcpConnection &connection,
                        WebSocketTransport *ws_transport, WriteBuffer frame,
                        bool is_websocket) {
  if (is_websocket) {
    if (ws_transport != nullptr) {
      (void)ws_transport->write_frame(frame);
    }
    return;
  }
  (void)connection.write(frame);
}

TransportReadChunk read_transport_chunk(TcpConnection &connection,
                                        WebSocketTransport *ws_transport) {
  if (ws_transport != nullptr) {
    WsReadChunk ws_chunk = ws_transport->read_chunk();
    return {.data = std::move(ws_chunk.data),
            .timed_out = ws_chunk.timed_out,
            .eof = ws_chunk.eof,
            .error = false};
  }

  std::array<uint8_t, k_transport_read_chunk_size> raw_buffer{};
  const std::ptrdiff_t bytes_read = connection.read(raw_buffer);
  if (bytes_read > 0) {
    return {.data = std::vector<uint8_t>(
                raw_buffer.begin(),
                raw_buffer.begin() + static_cast<std::size_t>(bytes_read)),
            .timed_out = false,
            .eof = false,
            .error = false};
  }

  if (bytes_read == 0) {
    return {.data = {}, .timed_out = false, .eof = true, .error = false};
  }

  if (connection.last_read_timed_out()) {
    return {.data = {}, .timed_out = true, .eof = false, .error = false};
  }

  return {.data = {}, .timed_out = false, .eof = false, .error = true};
}

void set_receive_timeout(TcpConnection &connection,
                         WebSocketTransport *ws_transport,
                         uint32_t timeout_millis) {
  if (ws_transport != nullptr) {
    ws_transport->set_receive_timeout(timeout_millis);
    return;
  }
  connection.set_receive_timeout(timeout_millis);
}

std::optional<AnyPacket> try_decode_packet(StreamBuffer &stream_buffer) {
  if (!stream_buffer.has_complete_packet()) {
    return std::nullopt;
  }

  const std::vector<uint8_t> packet_bytes = stream_buffer.consume_packet();
  const uint8_t packet_type_nibble =
      packet_bytes.empty() ? 0U : static_cast<uint8_t>(packet_bytes.front() >> 4U);
  ReadBuffer read_buffer(packet_bytes);
  try {
    return read_packet(read_buffer);
  } catch (const CodecException &exception) {
    if (packet_type_nibble == static_cast<uint8_t>(PacketType::Connack)) {
      throw CodecException{CodecError::InvalidPacketType,
                           "CONNACK is not valid from client during runtime"};
    }
    throw;
  }
}

bool send_connack_and_stop(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, const std::function<void()> &stop_transport,
    ReasonCode reason_code, const std::vector<Property> &properties) {
  write_frame_direct(
      connection, ws_transport,
      encode_connack_packet(ConnackPacket{.session_present = false,
                                          .reason_code = reason_code,
                                          .properties = properties}),
      is_websocket);
  stop_transport();
  return false;
}

bool send_v311_reject_and_stop(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, const std::function<void()> &stop_transport) {
  // MQTT 3.1.1 §3.2.2.3: CONNACK with return code 1 = Unacceptable Protocol Version.
  // Fixed format: 0x20 0x02 0x00 0x01 (no properties).
  static constexpr std::array<uint8_t, 4> k_v311_reject{0x20, 0x02, 0x00, 0x01};
  WriteBuffer frame;
  frame.insert(frame.end(), k_v311_reject.begin(), k_v311_reject.end());
  write_frame_direct(connection, ws_transport, std::move(frame), is_websocket);
  stop_transport();
  return false;
}

void write_error_disconnect(TcpConnection &connection,
                            WebSocketTransport *ws_transport,
                            bool is_websocket,
                            const ConnectPacket &connect_packet,
                            RuntimeDisconnectState &disconnect_state,
                            ReasonCode reason_code,
                            std::string_view reason_text) {
  disconnect_state.clean_disconnect = true;
  disconnect_state.reason_code = reason_code;
  write_frame_direct(
      connection, ws_transport,
      encode_disconnect_packet(
          reason_code,
          build_protocol_error_disconnect_properties(connect_packet,
                                                    reason_text)),
      is_websocket);
}

} // namespace mqtt
