/**
 * @file connect_phase_flow.cpp
 * @brief CONNECT and authentication phase orchestration.
 */

#include "connection/connect_phase_flow.h"

#include <chrono>
#include <string>
#include <string_view>

#include "codec/codec_error.h"
#include "connection/connection_error.h"
#include "connection/connection_flow_support.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/write_queue.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr auto k_connect_handshake_timeout = std::chrono::seconds(30);

[[nodiscard]] bool handshake_expired(
    std::chrono::steady_clock::time_point handshake_deadline) {
  return std::chrono::steady_clock::now() >= handshake_deadline;
}

void emit_handshake_abort_event(Broker &broker, std::string_view phase,
                                std::string_view reason,
                                std::string_view client_id = {}) {
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Info, "connection") {
    TraceEvent event;
    event.level = TraceLevel::Info;
    event.module = "connection";
    event.info = "connect_handshake_aborted";
    event.data.emplace_back("phase", std::string(phase));
    event.data.emplace_back("reason", std::string(reason));
    if (!client_id.empty()) {
      event.data.emplace_back("client_id", std::string(client_id));
    }
    broker.structured_tracer().emit(event);
  }
}

void mark_auth_failure(ConnectResult &connect_result, ReasonCode reason_code) {
  connect_result.auth_status = AuthStatus::Failure;
  connect_result.reason_code = reason_code;
}

void process_auth_packets_from_buffer(Broker &broker, StreamBuffer &stream_buffer,
                                      ConnectResult &connect_result,
                                      bool &got_auth_packet) {
  while (true) {
    std::optional<AnyPacket> auth_any;
    try {
      auth_any = try_decode_packet(stream_buffer);
    } catch (const CodecException &codec_exception) {
      mark_auth_failure(
          connect_result,
          map_codec_error_to_connect_reason(codec_exception.error()));
      got_auth_packet = true;
      return;
    } catch (...) {
      mark_auth_failure(connect_result, ReasonCode::ProtocolError);
      got_auth_packet = true;
      return;
    }

    if (!auth_any.has_value()) {
      return;
    }

    if (!std::holds_alternative<AuthPacket>(*auth_any)) {
      mark_auth_failure(connect_result, ReasonCode::ProtocolError);
      got_auth_packet = true;
      return;
    }

    connect_result = broker.handle_auth_packet(
        connect_result.client_id, std::get<AuthPacket>(*auth_any));
    got_auth_packet = true;
    return;
  }
}

[[nodiscard]] bool wait_for_auth_packet(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    Broker &broker, StreamBuffer &stream_buffer, ConnectResult &connect_result,
    const std::function<void()> &stop_transport,
    std::chrono::steady_clock::time_point handshake_deadline) {
  bool got_auth_packet = false;
  while (broker.is_running() && !got_auth_packet) {
    if (handshake_expired(handshake_deadline)) {
      emit_handshake_abort_event(broker, "auth", "timeout",
                                 connect_result.client_id);
      stop_transport();
      return false;
    }

    TransportReadChunk auth_chunk = read_transport_chunk(connection, ws_transport);
    if (auth_chunk.error || auth_chunk.eof) {
      emit_handshake_abort_event(
          broker, "auth", auth_chunk.error ? "transport_error" : "peer_closed",
          connect_result.client_id);
      stop_transport();
      return false;
    }

    if (auth_chunk.timed_out || auth_chunk.data.empty()) {
      continue;
    }

    stream_buffer.append(auth_chunk.data);
    process_auth_packets_from_buffer(broker, stream_buffer, connect_result,
                                     got_auth_packet);
  }

  return true;
}

[[nodiscard]] bool complete_connect_authentication(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, Broker &broker, StreamBuffer &stream_buffer,
    WriteQueue &write_queue, ConnectResult &connect_result,
    const std::function<void()> &stop_transport,
    std::chrono::steady_clock::time_point handshake_deadline) {
  while (connect_result.auth_status == AuthStatus::Continue) {
    if (handshake_expired(handshake_deadline)) {
      emit_handshake_abort_event(broker, "auth_continue", "timeout",
                                 connect_result.client_id);
      stop_transport();
      return false;
    }

    const std::vector<Property> auth_properties =
        build_auth_properties(connect_result.auth_method,
                              connect_result.auth_data,
                              !connect_result.auth_method.empty());
    if (!enqueue_frame(write_queue,
                       encode_auth_packet(connect_result.reason_code,
                                          auth_properties),
                       is_websocket)) {
      stop_transport();
      return false;
    }
    if (!wait_for_auth_packet(connection, ws_transport, broker,
                              stream_buffer, connect_result, stop_transport,
                              handshake_deadline)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool process_connect_packets_from_buffer(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, Broker &broker, StreamBuffer &stream_buffer,
    WriteQueue &write_queue, std::optional<ConnectPacket> &connect_packet,
    ConnectResult &connect_result,
    std::atomic<bool> &session_takeover_requested,
    const std::function<void()> &stop_transport,
    std::chrono::steady_clock::time_point handshake_deadline) {
  while (true) {
    std::optional<AnyPacket> packet_any;
    try {
      packet_any = try_decode_packet(stream_buffer);
    } catch (const CodecException &codec_exception) {
      if (codec_exception.error() == CodecError::InvalidProtocolVersion &&
          codec_exception.detected_protocol_version().has_value()) {
        // Non-v5 CONNECT: send a version-appropriate rejection.
        // MQTT 3.1.1 (version byte 4) requires a 2-byte CONNACK (return code 1).
        // All other non-v5 versions receive the MQTT 5.0 rejection.
        if (codec_exception.detected_protocol_version().value() == 0x04U) {
          return send_v311_reject_and_stop(connection, ws_transport,
                                           is_websocket, stop_transport);
        }
      }
      return send_connack_and_stop(
          connection, ws_transport, is_websocket, stop_transport,
          map_codec_error_to_connect_reason(codec_exception.error()));
    } catch (...) {
      return send_connack_and_stop(connection, ws_transport, is_websocket,
                                   stop_transport,
                                   ReasonCode::ProtocolError);
    }

    if (!packet_any.has_value()) {
      return true;
    }

    if (!std::holds_alternative<ConnectPacket>(*packet_any)) {
      return send_connack_and_stop(connection, ws_transport, is_websocket,
                                   stop_transport,
                                   ReasonCode::ProtocolError);
    }

    connect_packet = std::get<ConnectPacket>(*packet_any);
    connect_result = broker.handle_connect(*connect_packet,
                                           [&session_takeover_requested]() {
                                             session_takeover_requested.store(
                                                 true,
                                                 std::memory_order_release);
                                           });

    if (!complete_connect_authentication(connection, ws_transport,
                                         is_websocket, broker,
                                         stream_buffer, write_queue,
                                         connect_result, stop_transport,
                                         handshake_deadline)) {
      return false;
    }

    if (connect_result.auth_status != AuthStatus::Success ||
        connect_result.reason_code != ReasonCode::Success) {
      return send_connack_and_stop(
          connection, ws_transport, is_websocket, stop_transport,
          connect_result.reason_code, connect_result.connack_properties);
    }

    if (!enqueue_frame(
        write_queue,
        encode_connack_packet(ConnackPacket{
          .session_present = connect_result.session_present,
          .reason_code = ReasonCode::Success,
          .properties = connect_result.connack_properties,
        }),
        is_websocket)) {
      stop_transport();
      return false;
    }
    return true;
  }
}

} // namespace

bool establish_connect_session(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, Broker &broker, StreamBuffer &stream_buffer,
    WriteQueue &write_queue, std::optional<ConnectPacket> &connect_packet,
    ConnectResult &connect_result,
    std::atomic<bool> &session_takeover_requested,
    const std::function<void()> &stop_transport,
    std::chrono::steady_clock::time_point handshake_deadline) {
  if (handshake_deadline == std::chrono::steady_clock::time_point{}) {
    handshake_deadline = std::chrono::steady_clock::now() + k_connect_handshake_timeout;
  }

  while (broker.is_running() && !connect_packet.has_value()) {
    if (handshake_expired(handshake_deadline)) {
      emit_handshake_abort_event(broker, "connect", "timeout");
      stop_transport();
      return false;
    }

    TransportReadChunk chunk = read_transport_chunk(connection, ws_transport);
    if (chunk.error || chunk.eof) {
      emit_handshake_abort_event(broker, "connect",
                                 chunk.error ? "transport_error" : "peer_closed");
      stop_transport();
      return false;
    }

    if (chunk.timed_out || chunk.data.empty()) {
      continue;
    }

    stream_buffer.append(chunk.data);
    if (!process_connect_packets_from_buffer(
            connection, ws_transport, is_websocket, broker, stream_buffer,
            write_queue, connect_packet, connect_result,
            session_takeover_requested, stop_transport, handshake_deadline)) {
      return false;
    }
  }

  if (!connect_packet.has_value()) {
    emit_handshake_abort_event(broker, "connect", "broker_stopped_before_connect");
    stop_transport();
    return false;
  }

  return true;
}

} // namespace mqtt
