#pragma once

/**
 * @file connection_flow_support.h
 * @brief Shared helpers for connection flow phases.
 */

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "codec/packet_reader/packet_reader.h"
#include "codec/write_buffer.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"

namespace mqtt {

/**
 * @brief Forward declaration of StreamBuffer.
 */
class StreamBuffer;
/**
 * @brief Forward declaration of TcpConnection.
 */
class TcpConnection;
/**
 * @brief Forward declaration of WebSocketTransport.
 */
class WebSocketTransport;

/**
 * @brief Runtime disconnect metadata attached to a connection session.
 */
struct RuntimeDisconnectState {
  bool clean_disconnect{false};
  ReasonCode reason_code{ReasonCode::Success};
  std::optional<uint32_t> expiry_override;
};

/**
 * @brief Encode CONNACK packet into write buffer.
 * @param packet CONNACK packet model.
 * @return Encoded wire buffer.
 */
[[nodiscard]] WriteBuffer encode_connack_packet(const ConnackPacket &packet);
/**
 * @brief Encode SUBACK packet into write buffer.
 * @param packet SUBACK packet model.
 * @return Encoded wire buffer.
 */
[[nodiscard]] WriteBuffer encode_suback_packet(const SubackPacket &packet);
/**
 * @brief Encode UNSUBACK packet into write buffer.
 * @param packet UNSUBACK packet model.
 * @return Encoded wire buffer.
 */
[[nodiscard]] WriteBuffer encode_unsuback_packet(const UnsubackPacket &packet);
/**
 * @brief Encode PINGRESP packet into write buffer.
 * @return Encoded wire buffer.
 */
[[nodiscard]] WriteBuffer encode_pingresp_packet();
[[nodiscard]] WriteBuffer encode_disconnect_packet(
    ReasonCode reason_code, const std::vector<Property> &properties = {});
[[nodiscard]] WriteBuffer encode_auth_packet(
    ReasonCode reason_code, const std::vector<Property> &properties = {});

[[nodiscard]] std::vector<Property> build_auth_properties(
    std::string_view auth_method, const std::optional<BinaryData> &auth_data,
    bool include_method);

[[nodiscard]] std::optional<uint32_t> find_session_expiry_override(
    const std::vector<Property> &properties);
[[nodiscard]] std::optional<uint32_t> find_maximum_packet_size(
    const std::vector<Property> &properties);
[[nodiscard]] std::optional<uint16_t> find_receive_maximum(
    const std::vector<Property> &properties);
[[nodiscard]] std::vector<Property> build_protocol_error_disconnect_properties(
    const ConnectPacket &connect_packet, std::string_view reason_text);

void write_frame_direct(TcpConnection &connection,
                        WebSocketTransport *ws_transport, WriteBuffer frame,
                        bool is_websocket);

void set_receive_timeout(TcpConnection &connection,
                         WebSocketTransport *ws_transport,
                         uint32_t timeout_millis);

[[nodiscard]] std::optional<AnyPacket> try_decode_packet(
    StreamBuffer &stream_buffer);

/**
 * @brief Send CONNACK and stop transport when send succeeds.
 * @param connection TCP connection to write to.
 * @param ws_transport Optional WebSocket transport.
 * @param is_websocket True when connection uses WebSocket framing.
 * @param stop_transport Callback to stop transport loop.
 * @param reason_code CONNACK reason code.
 * @param properties CONNACK properties to include.
 * @return True when packet send path completed.
 */
[[nodiscard]] bool send_connack_and_stop(TcpConnection &connection, WebSocketTransport *ws_transport, bool is_websocket, const std::function<void()> &stop_transport, ReasonCode reason_code, const std::vector<Property> &properties = {});

/**
 * @brief Send MQTT 3.1.1 CONNACK reject packet and stop transport.
 * @param connection TCP connection to write to.
 * @param ws_transport Optional WebSocket transport.
 * @param is_websocket True when connection uses WebSocket framing.
 * @param stop_transport Callback to stop transport loop.
 * @return True when reject packet send path completed.
 */
[[nodiscard]] bool send_v311_reject_and_stop(TcpConnection &connection, WebSocketTransport *ws_transport, bool is_websocket, const std::function<void()> &stop_transport);

void write_error_disconnect(TcpConnection &connection,
                            WebSocketTransport *ws_transport,
                            bool is_websocket,
                            const ConnectPacket &connect_packet,
                            RuntimeDisconnectState &disconnect_state,
                            ReasonCode reason_code,
                            std::string_view reason_text);

} // namespace mqtt
