/**
 * @file client_handler.cpp
 * @brief ClientHandler lean implementation.
 */

#include "connection/client_handler.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

#include "auth/authenticator.h"
#include "broker/broker.h"
#include "broker/broker_config.h"
#include "client_session/client_session.h"
#include "connection/connect_phase_flow.h"
#include "connection/connection_flow_support.h"
#include "connection/runtime_phase_flow.h"
#include "monitoring/structured_tracer.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/write_queue.h"
#include "outbound_queue/outbound_queue.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

constexpr uint16_t k_default_receive_maximum = 65535U;
constexpr auto k_connect_handshake_timeout = std::chrono::seconds(30);

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

void ClientHandler::run(std::unique_ptr<TcpConnection> conn, Broker &broker,
                        const BrokerConfig &config, bool is_ws) {
  last_run_started_ = std::chrono::steady_clock::now();
  run_client_handler_flow(std::move(conn), broker, config, is_ws);
}

} // namespace mqtt
