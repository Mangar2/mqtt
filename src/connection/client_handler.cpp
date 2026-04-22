/**
 * @file client_handler.cpp
 * @brief ClientHandler lean implementation.
 */

#include "connection/client_handler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "auth/authenticator.h"
#include "broker/broker.h"
#include "broker/broker_config.h"
#include "client_session/client_session.h"
#include "connection/close_step.h"
#include "connection/connect_phase_flow.h"
#include "connection/connection_flow_support.h"
#include "connection/decode_step.h"
#include "connection/handshake_step.h"
#include "connection/outbound_drain_step.h"
#include "connection/runtime_phase_flow.h"
#include "connection/runtime_step.h"
#include "executor/job_scheduler.h"
#include "monitoring/structured_tracer.h"
#include "network/connection_slot.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/write_queue.h"
#include "outbound_queue/outbound_queue.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr uint16_t k_default_receive_maximum = 65535U;
constexpr auto k_connect_handshake_timeout = std::chrono::seconds(30);
constexpr std::size_t k_decode_read_chunk_size = 4096U;
constexpr std::size_t k_decode_read_budget_bytes = 64U * 1024U;
constexpr std::size_t k_decode_packet_budget = 32U;

void trace_connection_registration(Broker &broker,
                                   const ConnectResult &connect_result) {
  TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Trace,
              "connection") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "connection";
    event.info = "connection_registered";
    event.data.emplace_back("client_id", connect_result.client_id);
    event.data.emplace_back("session_present", connect_result.session_present ? "true" : "false");
    broker.structured_tracer().emit(event);
  }
}

void flush_resumed_session_if_needed(Broker &broker,
                                     const ConnectResult &connect_result) {
  if (!connect_result.session_present) {
    return;
  }

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

bool append_frame_to_slot(ConnectionSlot &slot, const WriteBuffer &frame,
                          bool is_websocket) {
  if (!is_websocket) {
    return slot.push_write_bytes(std::span<const uint8_t>(frame.data(), frame.size()));
  }

  const std::vector<uint8_t> ws_frame =
      WebSocketTransport::encode_frame(std::span<const uint8_t>(frame.data(), frame.size()));
  return slot.push_write_bytes(
      std::span<const uint8_t>(ws_frame.data(), ws_frame.size()));
}

bool move_pending_frames_to_slot(ConnectionSession &session, ConnectionSlot &slot) {
  for (const WriteBuffer &frame : session.pending_write_frames()) {
    if (!append_frame_to_slot(slot, frame, session.is_websocket())) {
      return false;
    }
  }
  session.clear_pending_write_frames();
  return true;
}

bool drain_socket_write_buffer(ConnectionSlot &slot) {
  while (slot.write_size() > 0U) {
    const std::span<const uint8_t> chunk = slot.write_contiguous_bytes();
    if (chunk.empty()) {
      return true;
    }

    std::size_t bytes_written = 0U;
    const IoResult write_result = nb_write(slot.fd(), chunk, &bytes_written);
    if (write_result == IoResult::Ok) {
      if (bytes_written == 0U) {
        return false;
      }
      (void)slot.pop_write_bytes(bytes_written);
      continue;
    }
    if (write_result == IoResult::WouldBlock) {
      return true;
    }
    return false;
  }
  return true;
}

void run_client_handler_flow(std::unique_ptr<TcpConnection> conn, Broker &broker,
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
    TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Warning, "connection") {
      TraceEvent event;
      event.level = TraceLevel::Warning;
      event.module = "connection";
      event.info = "websocket_transport_init_failed";
      broker.structured_tracer().emit(event);
    }
    conn->close();
    return;
  }

  const uint32_t timeout_millis =
      (config.tick_interval_ms > 0U) ? config.tick_interval_ms : 100U;
  set_receive_timeout(*conn, ws_transport.get(), timeout_millis);

  StreamBuffer stream_buffer;
  WriteQueue write_queue(static_cast<std::size_t>(config.write_queue_max_bytes));

  write_queue.set_sink([connection = conn.get(), ws_instance = ws_transport.get(),
                        is_websocket](std::span<const uint8_t> frame) {
    if (is_websocket) {
      if (ws_instance == nullptr) {
        return false;
      }
      std::vector<uint8_t> copy(frame.begin(), frame.end());
      return ws_instance->tcp().write(copy);
    }
    return connection != nullptr && connection->write(frame);
  });

  const auto stop_transport = [&write_queue, &conn]() {
    write_queue.stop();
    conn->close();
  };

  std::optional<ConnectPacket> connect_packet;
  ConnectResult connect_result;
  std::atomic<bool> session_takeover_requested{false};
  const auto handshake_deadline =
      std::chrono::steady_clock::now() + k_connect_handshake_timeout;

  if (!establish_connect_session(*conn, ws_transport.get(), is_websocket,
                                 broker, stream_buffer, write_queue,
                                 connect_packet, connect_result,
                                 session_takeover_requested,
                                 stop_transport, handshake_deadline)) {
    return;
  }

  std::shared_ptr<OutboundQueue> outbound_queue =
      std::make_shared<OutboundQueue>(
          static_cast<std::size_t>(config.max_queued_messages));
  broker.register_connection(connect_result.client_id, outbound_queue);
  write_queue.set_tracer(&broker.structured_tracer(), connect_result.client_id);
  trace_connection_registration(broker, connect_result);
  flush_resumed_session_if_needed(broker, connect_result);

  std::shared_ptr<IAuthenticator> authenticator(
      &broker.authenticator(), [](IAuthenticator * /*unused*/) {});

  const std::string username =
      connect_packet->username.has_value() ? connect_packet->username->value : "";
  const uint32_t maximum_packet_size =
      find_maximum_packet_size(connect_packet->properties).value_or(0U);
  const auto receive_maximum =
      find_receive_maximum(connect_packet->properties);
  if (receive_maximum.has_value() && receive_maximum.value() == 0U) {
    TRACE_GUARD((&broker.structured_tracer()), TraceLevel::Warning, "connection") {
      TraceEvent event;
      event.level = TraceLevel::Warning;
      event.module = "connection";
      event.info = "connect_rejected_receive_maximum_zero";
      event.data.emplace_back("client_id", connect_result.client_id);
      broker.structured_tracer().emit(event);
    }
    stop_transport();
    return;
  }
  const uint16_t outbound_receive_maximum =
      receive_maximum.value_or(k_default_receive_maximum);
  const uint16_t effective_keep_alive =
      (config.server_keep_alive > 0U) ? config.server_keep_alive
                                      : connect_packet->keep_alive;

  ClientSession client_session(
      connect_result.client_id, username, std::move(authenticator),
      outbound_queue, broker.session_manager().inflight_store(),
      effective_keep_alive, outbound_receive_maximum,
      config.topic_alias_maximum,
      std::chrono::seconds(config.qos_retransmit_timeout_seconds),
      maximum_packet_size, connect_result.auth_method);
  client_session.set_tracer(&broker.structured_tracer());

  if (connect_result.session_present) {
    client_session.mark_session_resumed();
  }

  client_session.keep_alive_timer().reset();
  RuntimeDisconnectState disconnect_state;
  run_connected_session_loop(
      *conn, ws_transport.get(), is_websocket, *connect_packet,
      connect_result, session_takeover_requested, stream_buffer,
      client_session, broker, write_queue, disconnect_state,
      config.receive_maximum);

  const auto now = std::chrono::steady_clock::now();
  if (disconnect_state.clean_disconnect) {
    broker.handle_disconnect(connect_result.client_id,
                             disconnect_state.reason_code,
                             disconnect_state.expiry_override,
                             now, outbound_queue);
  } else {
    broker.handle_connection_lost(connect_result.client_id, now,
                                  outbound_queue);
  }

  stop_transport();
}

} // namespace

namespace client_handler {

void process_accept_job(const AcceptJobPayload &payload, ConnectionTable &table,
                        IoReactor &reactor, JobScheduler &scheduler,
                        Broker &broker, const BrokerConfig &config) {
  if (payload.socket_handle == k_invalid_socket) {
    return;
  }

  auto connection = std::make_unique<TcpConnection>(payload.socket_handle);
  if (set_nonblocking(payload.socket_handle) != IoResult::Ok) {
    connection->close();
    return;
  }

  std::unique_ptr<WebSocketTransport> ws_transport;
  if (payload.websocket_connection) {
    try {
      ws_transport = std::make_unique<WebSocketTransport>(*connection);
    } catch (...) {
      connection->close();
      return;
    }
  }

  auto session = std::make_unique<ConnectionSession>(
      std::move(connection), std::move(ws_transport),
      payload.websocket_connection, config);

  ConnectionSlot slot(payload.socket_handle);
  (void)slot.transition_to(ConnectionPhase::Connected);
  const int fd = static_cast<int>(payload.socket_handle);
  if (!table.add(fd, std::move(slot), std::move(session))) {
    return;
  }

  reactor.register_connection(
      fd,
      [&scheduler](int active_fd) {
        scheduler.submit(ConnectionJob{.type = JobType::Decode,
                                       .connection_fd = active_fd,
                                       .payload = DecodeJobPayload{}});
      },
      [&scheduler](int active_fd) {
        scheduler.submit(ConnectionJob{.type = JobType::Drain,
                                       .connection_fd = active_fd,
                                       .payload = DrainJobPayload{}});
      });

  scheduler.submit(ConnectionJob{.type = JobType::Decode,
                                 .connection_fd = fd,
                                 .payload = DecodeJobPayload{}});
  (void)broker;
}

void process_decode_job(int fd, ConnectionTable &table, IoReactor &reactor,
                        JobScheduler &scheduler, Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    return;
  }

  ConnectionSession &session = *entry->session;
  ConnectionSlot &slot = entry->slot;

  std::array<uint8_t, k_decode_read_chunk_size> read_chunk{};
  std::size_t total_read = 0U;
  while (total_read < k_decode_read_budget_bytes) {
    std::size_t bytes_read = 0U;
    const IoResult read_result =
        nb_read(slot.fd(), std::span<uint8_t>(read_chunk.data(), read_chunk.size()), &bytes_read);
    if (read_result == IoResult::Ok) {
      if (bytes_read == 0U) {
        scheduler.submit(ConnectionJob{.type = JobType::Close,
                                       .connection_fd = fd,
                                       .payload = CloseJobPayload{.immediate = false}});
        return;
      }
      session.stream_buffer().append(
          std::span<const uint8_t>(read_chunk.data(), bytes_read));
      total_read += bytes_read;
      continue;
    }
    if (read_result == IoResult::WouldBlock) {
      break;
    }

    scheduler.submit(ConnectionJob{.type = JobType::Close,
                                   .connection_fd = fd,
                                   .payload = CloseJobPayload{.immediate = false}});
    return;
  }

  std::size_t packets_processed = 0U;
  while (packets_processed < k_decode_packet_budget) {
    const DecodeOutcome outcome = decode_one_packet(session, broker);
    if (outcome == DecodeOutcome::NeedMore) {
      break;
    }
    if (outcome == DecodeOutcome::ProtocolError ||
        outcome == DecodeOutcome::Disconnected) {
      scheduler.submit(ConnectionJob{.type = JobType::Close,
                                     .connection_fd = fd,
                                     .payload = CloseJobPayload{.immediate = false}});
      break;
    }
    ++packets_processed;
  }

  drain_outbound_to_write_buffer(session, broker);
  if (!move_pending_frames_to_slot(session, slot)) {
    scheduler.submit(ConnectionJob{.type = JobType::Close,
                                   .connection_fd = fd,
                                   .payload = CloseJobPayload{.immediate = false}});
    return;
  }

  if (slot.write_size() > 0U) {
    reactor.arm_write(fd);
    scheduler.submit(ConnectionJob{.type = JobType::Drain,
                                   .connection_fd = fd,
                                   .payload = DrainJobPayload{}});
  }
}

void process_drain_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    return;
  }

  ConnectionSession &session = *entry->session;
  ConnectionSlot &slot = entry->slot;

  drain_outbound_to_write_buffer(session, broker);
  if (!move_pending_frames_to_slot(session, slot)) {
    process_close_job(fd, table, reactor, broker);
    return;
  }

  if (!drain_socket_write_buffer(slot)) {
    process_close_job(fd, table, reactor, broker);
    return;
  }

  if (slot.write_size() > 0U) {
    reactor.arm_write(fd);
    return;
  }
  reactor.disarm_write(fd);
}

void process_close_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker) {
  ConnectionTable::Entry *entry = table.find(fd);
  if (entry == nullptr || entry->session == nullptr) {
    reactor.unregister(fd);
    (void)table.remove(fd);
    return;
  }

  ConnectionSession &session = *entry->session;
  finalize_close(session, broker);
  session.connection().close();
  reactor.unregister(fd);
  (void)table.remove(fd);
}

} // namespace client_handler

void ClientHandler::run(std::unique_ptr<TcpConnection> conn, Broker &broker,
                        const BrokerConfig &config, bool is_ws) {
  last_run_started_ = std::chrono::steady_clock::now();
  run_client_handler_flow(std::move(conn), broker, config, is_ws);
}

} // namespace mqtt
