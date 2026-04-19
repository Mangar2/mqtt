/**
 * @file runtime_phase_flow.cpp
 * @brief Runtime packet loop for connected sessions.
 */

#include "connection/runtime_phase_flow.h"

#include <functional>

#include "broker/broker.h"
#include "client_session/client_session.h"
#include "codec/codec_error.h"
#include "codec/packet/publish_codec.h"
#include "connection/connection_error.h"
#include "connection/connection_flow_support.h"
#include "connection/receive_maximum.h"
#include "monitoring/structured_tracer.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/write_queue.h"
#include "qos/qos_error.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

struct RuntimePacketDispatchContext {
  TcpConnection &connection;
  WebSocketTransport *ws_transport;
  ClientSession &client_session;
  Broker &broker;
  WriteQueue &write_queue;
  bool is_websocket;
  const ConnectResult &connect_result;
  RuntimeDisconnectState &disconnect_state;
  ReceiveMaximum &inbound_receive_maximum;
  const std::function<void()> &write_protocol_error_disconnect;
};

void mark_clean_disconnect(RuntimeDisconnectState &disconnect_state,
                           ReasonCode reason_code,
                           std::optional<uint32_t> expiry_override =
                               std::nullopt) {
  disconnect_state.clean_disconnect = true;
  disconnect_state.reason_code = reason_code;
  disconnect_state.expiry_override = expiry_override;
}

class RuntimePacketDispatcher {
public:
  RuntimePacketDispatcher(RuntimePacketDispatchContext &context,
                         bool &should_break)
      : context_(context), should_break_(should_break) {}

  void operator()(const PublishPacket &packet) const {
    const auto encode_puback_frame = [](const PubackPacket &puback_packet) {
      WriteBuffer frame;
      encode_puback(frame, puback_packet);
      return frame;
    };
    const auto encode_pubrec_frame = [](const PubrecPacket &pubrec_packet) {
      WriteBuffer frame;
      encode_pubrec(frame, pubrec_packet);
      return frame;
    };

    const bool tracks_inbound_inflight = packet.qos == QoS::ExactlyOnce;
    if (tracks_inbound_inflight &&
        !context_.inbound_receive_maximum.acquire()) {
      mark_clean_disconnect(context_.disconnect_state,
                            ReasonCode::ReceiveMaximumExceeded);
      write_frame_direct(
          context_.connection, context_.ws_transport,
          encode_disconnect_packet(ReasonCode::ReceiveMaximumExceeded),
          context_.is_websocket);
      should_break_ = true;
      return;
    }

    InboundPublishResult publish_result =
        context_.client_session.on_publish(packet);

    if (tracks_inbound_inflight && !publish_result.routable_message.has_value()) {
      context_.inbound_receive_maximum.release();
    }

    ReasonCode publish_reason = ReasonCode::Success;
    if (publish_result.routable_message.has_value()) {
      Message routable_message = std::move(*publish_result.routable_message);
      publish_reason = context_.broker.handle_publish(
          routable_message, context_.client_session.client_id(),
          context_.client_session.username(),
          context_.client_session.topic_alias_table());

      if (packet.qos == QoS::ExactlyOnce && is_error(publish_reason) &&
          packet.packet_id.has_value()) {
        context_.client_session.abort_inbound_qos2(packet.packet_id.value());
        if (tracks_inbound_inflight) {
          context_.inbound_receive_maximum.release();
        }
      }
    }

    if (packet.qos == QoS::AtLeastOnce && packet.packet_id.has_value() &&
        !publish_result.response_frames.empty()) {
      const PubackPacket puback_packet{.packet_id = packet.packet_id.value(),
                                       .reason_code = publish_reason,
                                       .properties = {}};
      publish_result.response_frames[0] = encode_puback_frame(puback_packet);
    }

    if (packet.qos == QoS::ExactlyOnce && packet.packet_id.has_value() &&
        !publish_result.response_frames.empty() &&
        publish_result.routable_message.has_value()) {
      ReasonCode pubrec_reason = publish_reason;
      if (pubrec_reason == ReasonCode::NoMatchingSubscribers) {
        pubrec_reason = ReasonCode::Success;
      }
      const PubrecPacket pubrec_packet{.packet_id = packet.packet_id.value(),
                                       .reason_code = pubrec_reason,
                                       .properties = {}};
      publish_result.response_frames[0] = encode_pubrec_frame(pubrec_packet);
    }

    for (WriteBuffer frame : publish_result.response_frames) {
      enqueue_frame(context_.write_queue, std::move(frame),
                    context_.is_websocket);
    }
  }

  void operator()(const SubscribePacket &packet) const {
    const SubackPacket suback =
        context_.broker.handle_subscribe(context_.client_session.client_id(),
                                         packet);
    enqueue_frame(context_.write_queue, encode_suback_packet(suback),
                  context_.is_websocket);
  }

  void operator()(const UnsubscribePacket &packet) const {
    const UnsubackPacket unsuback =
        context_.broker.handle_unsubscribe(context_.client_session.client_id(),
                                           packet);
    enqueue_frame(context_.write_queue, encode_unsuback_packet(unsuback),
                  context_.is_websocket);
  }

  void operator()(const PubackPacket &packet) const {
    context_.client_session.on_puback(packet);
  }

  void operator()(const PubrecPacket &packet) const {
    enqueue_frame(context_.write_queue,
                  context_.client_session.on_pubrec(packet),
                  context_.is_websocket);
  }

  void operator()(const PubrelPacket &packet) const {
    WriteBuffer pubcomp_frame;
    try {
      pubcomp_frame = context_.client_session.on_pubrel(packet);
    } catch (const QosException &exception) {
      if (exception.error() != QosError::UnexpectedPacketId) {
        throw;
      }
      WriteBuffer frame;
      encode_pubcomp(
          frame,
          PubcompPacket{.packet_id = packet.packet_id,
                        .reason_code = ReasonCode::PacketIdentifierNotFound,
                        .properties = {}});
      pubcomp_frame = std::move(frame);
    }

    try {
      context_.inbound_receive_maximum.release();
    } catch (const ConnectionException & /*unused*/) {
    }
    enqueue_frame(context_.write_queue, pubcomp_frame, context_.is_websocket);
  }

  void operator()(const PubcompPacket &packet) const {
    context_.client_session.on_pubcomp(packet);
  }

  void operator()(const PingreqPacket & /*unused*/) const {
    enqueue_frame(context_.write_queue, encode_pingresp_packet(),
                  context_.is_websocket);
  }

  void operator()(const DisconnectPacket &packet) const {
    const std::optional<uint32_t> candidate_override =
        find_session_expiry_override(packet.properties);
    const bool override_valid =
        context_.broker.is_disconnect_expiry_override_valid(
            context_.connect_result.client_id, candidate_override);
    if (!override_valid) {
      context_.write_protocol_error_disconnect();
      should_break_ = true;
      return;
    }

    mark_clean_disconnect(context_.disconnect_state, packet.reason_code,
                          candidate_override);
    should_break_ = true;
  }

  void operator()(const AuthPacket &packet) const {
    const AuthResult auth_result = context_.client_session.on_auth(packet);
    if (auth_result.status == AuthStatus::Failure) {
      mark_clean_disconnect(context_.disconnect_state, auth_result.reason_code);
      write_frame_direct(
          context_.connection, context_.ws_transport,
          encode_disconnect_packet(auth_result.reason_code),
          context_.is_websocket);
      should_break_ = true;
      return;
    }

    const std::vector<Property> auth_properties =
      build_auth_properties(context_.client_session.negotiated_auth_method(),
                  auth_result.auth_data, true);
    enqueue_frame(context_.write_queue,
                  encode_auth_packet(auth_result.reason_code,
                                     auth_properties),
                  context_.is_websocket);
  }

  void operator()(const ConnectPacket & /*unused*/) const {
    context_.write_protocol_error_disconnect();
    should_break_ = true;
  }

  template <typename PacketType> void operator()(const PacketType & /*unused*/) const {
    context_.write_protocol_error_disconnect();
    should_break_ = true;
  }

private:
  RuntimePacketDispatchContext &context_;
  bool &should_break_;
};

void emit_session_takeover_disconnect(TcpConnection &connection,
                                      WebSocketTransport *ws_transport,
                                      bool is_websocket, Broker &broker,
                                      const ConnectResult &connect_result) {
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace,
              "connection") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "connection";
    event.info = "session_takeover_disconnect";
    event.data.emplace_back("client_id", connect_result.client_id);
    event.data.emplace_back("reason_code", "0x8E");
    broker.structured_tracer().emit(event);
  }

  write_frame_direct(connection, ws_transport,
                     encode_disconnect_packet(ReasonCode::SessionTakenOver),
                     is_websocket);
}

void dispatch_runtime_packet(
    const AnyPacket &packet_any, RuntimePacketDispatchContext &context,
    bool &should_break) {
  RuntimePacketDispatcher dispatcher(context, should_break);
  std::visit(dispatcher, packet_any);
}

[[nodiscard]] bool process_runtime_packets(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, const ConnectPacket &connect_packet,
    const ConnectResult &connect_result, StreamBuffer &stream_buffer,
    ClientSession &client_session, Broker &broker, WriteQueue &write_queue,
  RuntimeDisconnectState &disconnect_state,
  ReceiveMaximum &inbound_receive_maximum) {
  const auto write_protocol_error_disconnect = [&]() {
    write_error_disconnect(connection, ws_transport, is_websocket,
                           connect_packet, disconnect_state,
                           ReasonCode::ProtocolError,
                           "Protocol error: malformed or invalid packet sequence");
  };
  const auto write_malformed_packet_disconnect = [&]() {
    write_error_disconnect(
        connection, ws_transport, is_websocket, connect_packet,
        disconnect_state, ReasonCode::MalformedPacket,
        "Malformed packet: invalid MQTT frame structure or properties");
  };

  bool should_break = false;
  while (!should_break) {
    std::optional<AnyPacket> packet_any;
    try {
      packet_any = try_decode_packet(stream_buffer);
    } catch (const CodecException &codec_exception) {
      const ReasonCode runtime_reason =
          map_codec_error_to_runtime_reason(codec_exception.error());
      if (runtime_reason == ReasonCode::MalformedPacket) {
        write_malformed_packet_disconnect();
      } else {
        write_protocol_error_disconnect();
      }
      return true;
    } catch (...) {
      write_protocol_error_disconnect();
      return true;
    }

    if (!packet_any.has_value()) {
      break;
    }

    RuntimePacketDispatchContext dispatch_context{
        .connection = connection,
        .ws_transport = ws_transport,
        .client_session = client_session,
        .broker = broker,
        .write_queue = write_queue,
        .is_websocket = is_websocket,
        .connect_result = connect_result,
        .disconnect_state = disconnect_state,
        .inbound_receive_maximum = inbound_receive_maximum,
        .write_protocol_error_disconnect = write_protocol_error_disconnect,
    };

    try {
      dispatch_runtime_packet(*packet_any, dispatch_context, should_break);
    } catch (...) {
      write_protocol_error_disconnect();
      return true;
    }
  }

  return should_break;
}

} // namespace

void run_connected_session_loop(
    TcpConnection &connection, WebSocketTransport *ws_transport,
    bool is_websocket, const ConnectPacket &connect_packet,
    const ConnectResult &connect_result,
    std::atomic<bool> &session_takeover_requested, StreamBuffer &stream_buffer,
    ClientSession &client_session, Broker &broker, WriteQueue &write_queue,
    RuntimeDisconnectState &disconnect_state,
    uint16_t inbound_receive_maximum) {
  ReceiveMaximum inbound_receive_window(inbound_receive_maximum);

  while (broker.is_running()) {
    if (session_takeover_requested.exchange(false,
                                            std::memory_order_acq_rel)) {
      disconnect_state.clean_disconnect = true;
      disconnect_state.reason_code = ReasonCode::SessionTakenOver;
      emit_session_takeover_disconnect(connection, ws_transport, is_websocket,
                                       broker, connect_result);
      break;
    }

    const std::vector<WriteBuffer> outbound_frames =
        client_session.drain_outbound();
    for (WriteBuffer frame : outbound_frames) {
      enqueue_frame(write_queue, std::move(frame), is_websocket);
    }

    TransportReadChunk chunk = read_transport_chunk(connection, ws_transport);
    if (chunk.error || chunk.eof) {
      if (!disconnect_state.clean_disconnect &&
          (Broker::shutdown_requested() || !broker.is_running())) {
        disconnect_state.clean_disconnect = true;
        disconnect_state.reason_code = ReasonCode::ServerShuttingDown;
        write_frame_direct(
            connection, ws_transport,
            encode_disconnect_packet(ReasonCode::ServerShuttingDown),
            is_websocket);
      }
      break;
    }

    if (chunk.timed_out) {
      if (client_session.keep_alive_timer().is_expired()) {
        disconnect_state.reason_code = ReasonCode::KeepAliveTimeout;
        disconnect_state.clean_disconnect = true;
        write_frame_direct(connection, ws_transport,
                           encode_disconnect_packet(
                               ReasonCode::KeepAliveTimeout),
                           is_websocket);
        break;
      }
      continue;
    }

    if (chunk.data.empty()) {
      continue;
    }

    client_session.keep_alive_timer().reset();
    stream_buffer.append(chunk.data);
    if (process_runtime_packets(connection, ws_transport, is_websocket,
                                connect_packet, connect_result, stream_buffer,
                                client_session, broker, write_queue,
                                disconnect_state,
                                inbound_receive_window)) {
      break;
    }
  }

  if (!disconnect_state.clean_disconnect && !broker.is_running()) {
    disconnect_state.clean_disconnect = true;
    disconnect_state.reason_code = ReasonCode::ServerShuttingDown;
    write_frame_direct(connection, ws_transport,
                       encode_disconnect_packet(ReasonCode::ServerShuttingDown),
                       is_websocket);
  }
}

} // namespace mqtt
