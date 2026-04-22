#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "codec/packet/connect_codec.h"
#include "client_session/client_session.h"
#include "connection/client_handler.h"
#include "connection/connect_phase_flow.h"
#include "connection/runtime_phase_flow.h"
#include "executor/job_queue.h"
#include "executor/job_scheduler.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/write_queue.h"

using namespace mqtt;

namespace {

BrokerConfig make_config() {
  BrokerConfig config;
  config.mqtt_port = 0U;
  config.ws_port = 0U;
  config.allow_anonymous = true;
  config.persistence_mode = PersistenceMode::Off;
  config.tick_interval_ms = 100U;
  return config;
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
  (void)::setsockopt(static_cast<SOCKET>(socket_handle), SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeout_value),
                     static_cast<int>(sizeof(timeout_value)));
#else
  struct timeval timeout_value {};
  timeout_value.tv_sec = static_cast<time_t>(timeout_millis / 1000U);
  timeout_value.tv_usec = static_cast<suseconds_t>((timeout_millis % 1000U) * 1000U);
  (void)::setsockopt(static_cast<int>(socket_handle), SOL_SOCKET, SO_RCVTIMEO,
                     &timeout_value, sizeof(timeout_value));
#endif
}

std::pair<SocketHandle, SocketHandle> make_connected_socket_pair() {
#ifdef _WIN32
  return {k_invalid_socket, k_invalid_socket};
#else
  int sockets[2] = {-1, -1};
  const int result = ::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
  REQUIRE(result == 0);
  return {static_cast<SocketHandle>(sockets[0]),
          static_cast<SocketHandle>(sockets[1])};
#endif
}

} // namespace

TEST_CASE("process_accept_and_close_job_lifecycle", "[connection]") {
#ifdef _WIN32
  SUCCEED("socketpair-based coverage test is POSIX-only");
#else
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();

  IoReactor reactor;
  reactor.start();
  JobQueue queue;
  JobScheduler scheduler(queue);
  ConnectionTable table;

  const auto socket_pair = make_connected_socket_pair();
  const SocketHandle accepted_socket = socket_pair.first;
  const SocketHandle client_socket = socket_pair.second;
  set_recv_timeout(client_socket, 1000U);

  const int connection_fd = static_cast<int>(accepted_socket);
  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);

  REQUIRE(table.find(connection_fd) != nullptr);

  const WriteBuffer connect_frame = []() {
    ConnectPacket packet;
    packet.client_id = Utf8String{"job-client"};
    packet.keep_alive = 10U;
    packet.clean_start = true;
    WriteBuffer frame;
    encode_connect(frame, packet);
    return frame;
  }();

  const int write_result = ::send(static_cast<int>(client_socket),
                                  connect_frame.data(), connect_frame.size(),
                                  0);
  REQUIRE(write_result >= 0);

  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);
  ConnectionTable::Entry *decoded_entry = table.find(connection_fd);
  REQUIRE(decoded_entry != nullptr);
  CHECK(decoded_entry->session->phase() == ConnectionSession::Phase::Connected);

  client_handler::process_drain_job(connection_fd, table, reactor, broker);
  CHECK(decoded_entry->slot.write_size() <= decoded_entry->slot.write_capacity());

  client_handler::process_close_job(connection_fd, table, reactor, broker);
  CHECK(table.find(connection_fd) == nullptr);

  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_accept_job_ignores_invalid_socket", "[connection]") {
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();

  IoReactor reactor;
  reactor.start();
  JobQueue queue;
  JobScheduler scheduler(queue);
  ConnectionTable table;

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = k_invalid_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);

  CHECK(table.size() == 0U);
  reactor.stop();
  broker.shutdown();
}

TEST_CASE("establish_connect_session_times_out_when_deadline_is_expired",
          "[connection]") {
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();

  TcpConnection connection(k_invalid_socket);
  StreamBuffer stream_buffer;
  WriteQueue write_queue;
  std::optional<ConnectPacket> connect_packet;
  ConnectResult connect_result;
  std::atomic<bool> session_takeover_requested{false};
  bool stopped = false;

  const bool result = establish_connect_session(
      connection, nullptr, false, broker, stream_buffer, write_queue,
      connect_packet, connect_result, session_takeover_requested,
      [&stopped]() { stopped = true; },
      std::chrono::steady_clock::now() - std::chrono::milliseconds(1));

  CHECK_FALSE(result);
  CHECK(stopped);
  broker.shutdown();
}

TEST_CASE("run_connected_session_loop_marks_server_shutting_down_when_stopped",
          "[connection]") {
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();
  broker.shutdown();

  TcpConnection connection(k_invalid_socket);
  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"stopped-client"};
  connect_packet.clean_start = true;
  connect_packet.keep_alive = 10U;

  ConnectResult connect_result;
  connect_result.client_id = "stopped-client";
  connect_result.reason_code = ReasonCode::Success;

  std::shared_ptr<IAuthenticator> authenticator(
      &broker.authenticator(), [](IAuthenticator * /*unused*/) {});
  auto outbound_queue = std::make_shared<OutboundQueue>();
  ClientSession client_session(
      connect_result.client_id, "", std::move(authenticator), outbound_queue,
      broker.session_manager().inflight_store(), 10U, 65535U,
      config.topic_alias_maximum,
      std::chrono::seconds(config.qos_retransmit_timeout_seconds), 0U,
      connect_result.auth_method);

  std::atomic<bool> session_takeover_requested{false};
  StreamBuffer stream_buffer;
  WriteQueue write_queue;
  RuntimeDisconnectState disconnect_state;

  run_connected_session_loop(
      connection, nullptr, false, connect_packet, connect_result,
      session_takeover_requested, stream_buffer, client_session, broker,
      write_queue, disconnect_state, config.receive_maximum);

  CHECK(disconnect_state.clean_disconnect);
  CHECK(disconnect_state.reason_code == ReasonCode::ServerShuttingDown);
}