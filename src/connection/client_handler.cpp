/**
 * @file client_handler.cpp
 * @brief ClientHandler lean implementation.
 */

#include "connection/client_handler.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "auth/authenticator.h"
#include "broker/broker.h"
#include "broker/broker_config.h"
#include "client_session/client_session.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "connection/connection_error.h"
#include "connection/connection_state.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "network/tcp_connection.h"
#include "network/stream_buffer.h"
#include "network/write_queue.h"
#include "outbound_queue/outbound_queue.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr std::size_t k_transport_read_chunk_size = 4096U;

struct TransportReadChunk {
  std::vector<uint8_t> data;
  bool timed_out{false};
  bool eof{false};
  bool error{false};
};

template <class... Callables> struct OverloadedVisitor : Callables... {
  using Callables::operator()...;
};

[[nodiscard]] WriteBuffer encode_connack_packet(const ConnackPacket &packet) {
  WriteBuffer frame;
  encode_connack(frame, packet);
  return frame;
}

[[nodiscard]] WriteBuffer encode_suback_packet(const SubackPacket &packet) {
  WriteBuffer frame;
  encode_suback(frame, packet);
  return frame;
}

[[nodiscard]] WriteBuffer encode_unsuback_packet(const UnsubackPacket &packet) {
  WriteBuffer frame;
  encode_unsuback(frame, packet);
  return frame;
}

[[nodiscard]] WriteBuffer encode_pingresp_packet() {
  WriteBuffer frame;
  encode_pingresp(frame);
  return frame;
}

[[nodiscard]] WriteBuffer
encode_disconnect_packet(ReasonCode reason_code,
                         const std::vector<Property> &properties = {}) {
  WriteBuffer frame;
  encode_disconnect(frame,
                    DisconnectPacket{.reason_code = reason_code,
                                     .properties = properties});
  return frame;
}

[[nodiscard]] WriteBuffer
encode_auth_packet(ReasonCode reason_code,
                   const std::vector<Property> &properties = {}) {
  WriteBuffer frame;
  encode_auth(frame,
              AuthPacket{.reason_code = reason_code, .properties = properties});
  return frame;
}

[[nodiscard]] std::vector<Property>
build_auth_properties(std::string_view auth_method,
                      const std::optional<BinaryData> &auth_data,
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

[[nodiscard]] std::optional<uint32_t>
find_session_expiry_override(const std::vector<Property> &properties) {
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

[[nodiscard]] std::optional<uint32_t>
find_maximum_packet_size(const std::vector<Property> &properties) {
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

[[nodiscard]] bool
requests_problem_information(const std::vector<Property> &properties) {
  for (const Property &property : properties) {
    if (property.id != PropertyId::RequestProblemInformation) {
      continue;
    }
    if (const auto *request_ptr = std::get_if<uint8_t>(&property.value)) {
      return *request_ptr != 0U;
    }
    return true;
  }
  // MQTT 5 default is 1 when Request Problem Information is absent.
  return true;
}

[[nodiscard]] std::vector<Property>
build_protocol_error_disconnect_properties(const ConnectPacket &connect_packet,
                                          std::string_view reason_text) {
  if (!requests_problem_information(connect_packet.properties)) {
    return {};
  }

  return {Property{.id = PropertyId::ReasonString,
                   .value = Utf8String{std::string(reason_text)}}};
}

void enqueue_frame(WriteQueue &write_queue, WriteBuffer frame,
                   bool is_websocket) {
  if (is_websocket) {
    (void)write_queue.enqueue(WebSocketTransport::encode_frame(frame));
    return;
  }
  (void)write_queue.enqueue(std::move(frame));
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

[[nodiscard]] TransportReadChunk
read_transport_chunk(TcpConnection &connection,
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

[[nodiscard]] std::optional<AnyPacket>
try_decode_packet(StreamBuffer &stream_buffer) {
  if (!stream_buffer.has_complete_packet()) {
    return std::nullopt;
  }

  const std::vector<uint8_t> packet_bytes = stream_buffer.consume_packet();
  ReadBuffer read_buffer(packet_bytes);
  return read_packet(read_buffer);
}

} // namespace

void ClientHandler::run(std::unique_ptr<TcpConnection> conn, Broker &broker,
                        const BrokerConfig &config, bool is_ws) {
  if (!conn) {
    return;
  }

  const bool is_websocket = is_ws;
  std::unique_ptr<WebSocketTransport> ws_transport;
  try {
    if (is_websocket) {
      ws_transport = std::make_unique<WebSocketTransport>(*conn);
    }
  } catch (...) {
    conn->close();
    return;
  }

  const uint32_t timeout_millis =
      (config.tick_interval_ms > 0U) ? config.tick_interval_ms : 100U;
  set_receive_timeout(*conn, ws_transport.get(), timeout_millis);

  StreamBuffer stream_buffer;
  WriteQueue write_queue;
  std::thread drain_thread([
      &write_queue, connection = conn.get(), ws_instance = ws_transport.get(),
      is_websocket] {
    if (is_websocket) {
      write_queue.run_drain(ws_instance->tcp());
      return;
    }
    write_queue.run_drain(*connection);
  });

  const auto stop_transport = [&write_queue, &drain_thread, &conn]() {
    write_queue.stop();
    conn->close();
    if (drain_thread.joinable()) {
      drain_thread.join();
    }
  };

  std::optional<ConnectPacket> connect_packet;
  ConnectResult connect_result;
  std::atomic<bool> session_takeover_requested{false};

  while (broker.is_running() && !connect_packet.has_value()) {
    TransportReadChunk chunk = read_transport_chunk(*conn, ws_transport.get());
    if (chunk.error || chunk.eof) {
      stop_transport();
      return;
    }

    if (chunk.timed_out || chunk.data.empty()) {
      continue;
    }

    stream_buffer.append(chunk.data);

    while (true) {
      std::optional<AnyPacket> packet_any;
      try {
        packet_any = try_decode_packet(stream_buffer);
      } catch (...) {
        write_frame_direct(*conn, ws_transport.get(),
                           encode_connack_packet(
                               ConnackPacket{.session_present = false,
                                             .reason_code =
                                                 ReasonCode::ProtocolError,
                                             .properties = {}}),
                           is_websocket);
        stop_transport();
        return;
      }

      if (!packet_any.has_value()) {
        break;
      }

      if (!std::holds_alternative<ConnectPacket>(*packet_any)) {
        write_frame_direct(*conn, ws_transport.get(),
                           encode_connack_packet(
                               ConnackPacket{.session_present = false,
                                             .reason_code =
                                                 ReasonCode::ProtocolError,
                                             .properties = {}}),
                           is_websocket);
        stop_transport();
        return;
      }

      connect_packet = std::get<ConnectPacket>(*packet_any);
      connect_result = broker.handle_connect(*connect_packet,
                                             [&session_takeover_requested]() {
                                               session_takeover_requested.store(
                                                   true,
                                                   std::memory_order_release);
                                             });

      while (connect_result.auth_status == AuthStatus::Continue) {
        const std::vector<Property> auth_properties = build_auth_properties(
            connect_result.auth_method, connect_result.auth_data,
            !connect_result.auth_method.empty());
        enqueue_frame(write_queue,
                      encode_auth_packet(connect_result.reason_code,
                                         auth_properties),
                      is_websocket);

        bool got_auth_packet = false;
        while (broker.is_running() && !got_auth_packet) {
          TransportReadChunk auth_chunk =
              read_transport_chunk(*conn, ws_transport.get());
          if (auth_chunk.error || auth_chunk.eof) {
            stop_transport();
            return;
          }

          if (auth_chunk.timed_out || auth_chunk.data.empty()) {
            continue;
          }

          stream_buffer.append(auth_chunk.data);
          while (true) {
            std::optional<AnyPacket> auth_any;
            try {
              auth_any = try_decode_packet(stream_buffer);
            } catch (...) {
              connect_result.auth_status = AuthStatus::Failure;
              connect_result.reason_code = ReasonCode::ProtocolError;
              got_auth_packet = true;
              break;
            }

            if (!auth_any.has_value()) {
              break;
            }

            if (!std::holds_alternative<AuthPacket>(*auth_any)) {
              connect_result.auth_status = AuthStatus::Failure;
              connect_result.reason_code = ReasonCode::ProtocolError;
              got_auth_packet = true;
              break;
            }

            connect_result = broker.handle_auth_packet(
                connect_result.client_id, std::get<AuthPacket>(*auth_any));
            got_auth_packet = true;
            break;
          }
        }
      }

      if (connect_result.auth_status != AuthStatus::Success ||
          connect_result.reason_code != ReasonCode::Success) {
        write_frame_direct(*conn, ws_transport.get(),
                   encode_connack_packet(ConnackPacket{
                     .session_present = false,
                     .reason_code = connect_result.reason_code,
                     .properties = connect_result.connack_properties,
                   }),
                   is_websocket);
        stop_transport();
        return;
      }

      enqueue_frame(
          write_queue,
          encode_connack_packet(ConnackPacket{
              .session_present = connect_result.session_present,
              .reason_code = ReasonCode::Success,
              .properties = connect_result.connack_properties,
          }),
          is_websocket);
      break;
    }
  }

  if (!connect_packet.has_value()) {
    stop_transport();
    return;
  }

  std::shared_ptr<OutboundQueue> outbound_queue =
      std::make_shared<OutboundQueue>(
          static_cast<std::size_t>(config.max_queued_messages));
  broker.register_connection(connect_result.client_id, outbound_queue);
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace, "connection") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "connection";
    event.info = "connection_registered";
    event.data.push_back({"client_id", connect_result.client_id});
    event.data.push_back(
        {"session_present", connect_result.session_present ? "true" : "false"});
    broker.structured_tracer().emit(event);
  }

  if (connect_result.session_present) {
    TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace,
          "connection") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "connection";
      event.info = "flush_offline_queue_requested";
      event.data.push_back({"client_id", connect_result.client_id});
      broker.structured_tracer().emit(event);
    }
    broker.message_router().flush_offline_queue(connect_result.client_id);
  }

  std::shared_ptr<IAuthenticator> authenticator(
      &broker.authenticator(), [](IAuthenticator * /*unused*/) {});

  const std::string username =
      connect_packet->username.has_value() ? connect_packet->username->value : "";
    const uint32_t maximum_packet_size =
      find_maximum_packet_size(connect_packet->properties).value_or(0U);

  ClientSession client_session(
      connect_result.client_id, username, std::move(authenticator),
      outbound_queue, broker.session_manager().inflight_store(),
      connect_packet->keep_alive, config.receive_maximum,
      config.topic_alias_maximum,
      std::chrono::seconds(config.qos_retransmit_timeout_seconds),
      maximum_packet_size);

  if (connect_result.session_present) {
    client_session.mark_session_resumed();
  }

  client_session.keep_alive_timer().reset();

  bool clean_disconnect = false;
  ReasonCode disconnect_reason = ReasonCode::Success;
  std::optional<uint32_t> expiry_override;
  const auto write_protocol_error_disconnect = [&]() {
    clean_disconnect = true;
    disconnect_reason = ReasonCode::ProtocolError;
    write_frame_direct(
        *conn, ws_transport.get(),
        encode_disconnect_packet(
            ReasonCode::ProtocolError,
            build_protocol_error_disconnect_properties(
                *connect_packet,
                "Protocol error: malformed or invalid packet sequence")),
        is_websocket);
  };

  while (broker.is_running()) {
    if (session_takeover_requested.exchange(false,
                                            std::memory_order_acq_rel)) {
      clean_disconnect = true;
      disconnect_reason = ReasonCode::SessionTakenOver;
      TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace,
                  "connection") {
        TraceEvent event;
        event.level = TraceLevel::Trace;
        event.module = "connection";
        event.info = "session_takeover_disconnect";
        event.data.push_back({"client_id", connect_result.client_id});
        event.data.push_back({"reason_code", "0x8E"});
        broker.structured_tracer().emit(event);
      }
      write_frame_direct(*conn, ws_transport.get(),
                         encode_disconnect_packet(disconnect_reason),
                         is_websocket);
      break;
    }

    const std::vector<WriteBuffer> outbound_frames =
        client_session.drain_outbound();
    for (WriteBuffer frame : outbound_frames) {
      enqueue_frame(write_queue, std::move(frame), is_websocket);
    }

    TransportReadChunk chunk = read_transport_chunk(*conn, ws_transport.get());
    if (chunk.error || chunk.eof) {
      break;
    }

    if (chunk.timed_out) {
      if (client_session.keep_alive_timer().is_expired()) {
        disconnect_reason = ReasonCode::KeepAliveTimeout;
        clean_disconnect = true;
        write_frame_direct(*conn, ws_transport.get(),
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

    bool should_break = false;
    while (!should_break) {
      std::optional<AnyPacket> packet_any;
      try {
        packet_any = try_decode_packet(stream_buffer);
      } catch (...) {
        write_protocol_error_disconnect();
        should_break = true;
        break;
      }

      if (!packet_any.has_value()) {
        break;
      }

      try {
        std::visit(
            OverloadedVisitor{
                [&](const PublishPacket &packet) {
                  InboundPublishResult publish_result =
                      client_session.on_publish(packet);
                  if (publish_result.routable_message.has_value()) {
                    Message routable_message =
                        std::move(*publish_result.routable_message);
                    broker.handle_publish(routable_message,
                                         client_session.client_id(),
                                         client_session.username(),
                                         client_session.topic_alias_table());
                  }
                  for (WriteBuffer frame : publish_result.response_frames) {
                    enqueue_frame(write_queue, std::move(frame), is_websocket);
                  }
                },
                [&](const SubscribePacket &packet) {
                  const SubackPacket suback =
                      broker.handle_subscribe(client_session.client_id(),
                                              packet);
                  enqueue_frame(write_queue, encode_suback_packet(suback),
                                is_websocket);
                },
                [&](const UnsubscribePacket &packet) {
                  const UnsubackPacket unsuback =
                      broker.handle_unsubscribe(client_session.client_id(),
                                                packet);
                  enqueue_frame(write_queue, encode_unsuback_packet(unsuback),
                                is_websocket);
                },
                [&](const PubackPacket &packet) {
                  client_session.on_puback(packet);
                },
                [&](const PubrecPacket &packet) {
                  enqueue_frame(write_queue, client_session.on_pubrec(packet),
                                is_websocket);
                },
                [&](const PubrelPacket &packet) {
                  enqueue_frame(write_queue, client_session.on_pubrel(packet),
                                is_websocket);
                },
                [&](const PubcompPacket &packet) {
                  client_session.on_pubcomp(packet);
                },
                [&](const PingreqPacket & /*unused*/) {
                  enqueue_frame(write_queue, encode_pingresp_packet(),
                                is_websocket);
                },
                [&](const DisconnectPacket &packet) {
                  const auto candidate_override =
                      find_session_expiry_override(packet.properties);
                  const bool override_valid =
                      broker.is_disconnect_expiry_override_valid(
                          connect_result.client_id, candidate_override);
                  if (!override_valid) {
                    write_protocol_error_disconnect();
                    should_break = true;
                    return;
                  }

                  clean_disconnect = true;
                  disconnect_reason = packet.reason_code;
                  expiry_override = candidate_override;
                  should_break = true;
                },
                [&](const AuthPacket &packet) {
                  const AuthResult auth_result = client_session.on_auth(packet);
                  const std::vector<Property> auth_properties =
                      build_auth_properties({}, auth_result.auth_data, false);
                  enqueue_frame(write_queue,
                                encode_auth_packet(auth_result.reason_code,
                                                   auth_properties),
                                is_websocket);
                  if (auth_result.status == AuthStatus::Failure) {
                    clean_disconnect = true;
                    disconnect_reason = auth_result.reason_code;
                    should_break = true;
                  }
                },
                [&](const ConnectPacket & /*unused*/) {
                  write_protocol_error_disconnect();
                  should_break = true;
                },
                [&](const auto &packet) {
                  using PacketType = std::decay_t<decltype(packet)>;
                  if constexpr (!std::is_same_v<PacketType, PublishPacket> &&
                                !std::is_same_v<PacketType, SubscribePacket> &&
                                !std::is_same_v<PacketType, UnsubscribePacket> &&
                                !std::is_same_v<PacketType, PubackPacket> &&
                                !std::is_same_v<PacketType, PubrecPacket> &&
                                !std::is_same_v<PacketType, PubrelPacket> &&
                                !std::is_same_v<PacketType, PubcompPacket> &&
                                !std::is_same_v<PacketType, PingreqPacket> &&
                                !std::is_same_v<PacketType, DisconnectPacket> &&
                                !std::is_same_v<PacketType, AuthPacket> &&
                                !std::is_same_v<PacketType, ConnectPacket>) {
                    write_protocol_error_disconnect();
                    should_break = true;
                  }
                }},
            *packet_any);
      } catch (...) {
        write_protocol_error_disconnect();
        should_break = true;
      }
    }

    if (should_break) {
      break;
    }
  }

  const auto now = std::chrono::steady_clock::now();
  if (clean_disconnect) {
    broker.handle_disconnect(connect_result.client_id, disconnect_reason,
                             expiry_override, now, outbound_queue);
  } else {
    broker.handle_connection_lost(connect_result.client_id, now,
                                  outbound_queue);
  }

  stop_transport();
}

} // namespace mqtt
