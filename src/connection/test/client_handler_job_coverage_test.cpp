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
#include "auth/authenticator.h"
#include "codec/packet/connect_codec.h"
#include "client_session/client_session.h"
#include "connection/client_handler.h"
#include "connection/connection_flow_support.h"
#include "connection/connection_session.h"
#include "executor/job_queue.h"
#include "executor/job_scheduler.h"
#include "network/connection_slot.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"
#include "store/inflight_store.h"

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

  client_handler::process_drain_job(connection_fd, table, reactor, scheduler,
                                    broker);
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

TEST_CASE("process_decode_job_closes_on_empty_read", "[connection]") {
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
  const int connection_fd = static_cast<int>(accepted_socket);

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);
  REQUIRE(table.find(connection_fd) != nullptr);

  close_socket_handle(client_socket);
  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);
  client_handler::process_close_job(connection_fd, table, reactor, broker);
  CHECK(table.find(connection_fd) == nullptr);

  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_decode_job_malformed_packet_submits_close", "[connection]") {
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
  const int connection_fd = static_cast<int>(accepted_socket);

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);
  REQUIRE(table.find(connection_fd) != nullptr);

  const std::vector<uint8_t> malformed_packet{0x10U, 0x00U};
  const int write_result = ::send(static_cast<int>(client_socket),
                                  malformed_packet.data(),
                                  malformed_packet.size(), 0);
  REQUIRE(write_result >= 0);

  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);
  client_handler::process_close_job(connection_fd, table, reactor, broker);
  CHECK(table.find(connection_fd) == nullptr);

  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_drain_and_close_ignore_missing_entry", "[connection]") {
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();

  IoReactor reactor;
  reactor.start();
  JobQueue queue;
  JobScheduler scheduler(queue);
  ConnectionTable table;

  CHECK_NOTHROW(client_handler::process_drain_job(12345, table, reactor,
                                                  scheduler, broker));
  CHECK_NOTHROW(client_handler::process_close_job(12345, table, reactor, broker));

  reactor.stop();
  broker.shutdown();
}

TEST_CASE("process_accept_job_ignores_duplicate_fd", "[connection]") {
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
  const int connection_fd = static_cast<int>(accepted_socket);

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);
  REQUIRE(table.find(connection_fd) != nullptr);

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);
  CHECK(table.find(connection_fd) != nullptr);

  client_handler::process_close_job(connection_fd, table, reactor, broker);
  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_decode_job_handles_peer_eof_with_close_job", "[connection]") {
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
  const int connection_fd = static_cast<int>(accepted_socket);

  client_handler::process_accept_job(
      AcceptJobPayload{.socket_handle = accepted_socket,
                       .websocket_connection = false},
      table, reactor, scheduler, broker, config);
  REQUIRE(table.find(connection_fd) != nullptr);

  ::shutdown(static_cast<int>(client_socket), SHUT_WR);
  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);
  client_handler::process_close_job(connection_fd, table, reactor, broker);
  CHECK(table.find(connection_fd) == nullptr);

  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_drain_job_websocket_frame_path_and_write_error", "[connection]") {
  const BrokerConfig config = make_config();
  Broker broker(config);
  broker.startup();

  IoReactor reactor;
  reactor.start();
  JobQueue queue;
  JobScheduler scheduler(queue);
  ConnectionTable table;

  auto connection = std::make_unique<TcpConnection>(k_invalid_socket);
  auto session = std::make_unique<ConnectionSession>(
      std::move(connection), nullptr, true, config);
  session->pending_write_frames().push_back(encode_pingresp_packet());

  REQUIRE(table.add(static_cast<int>(k_invalid_socket),
                    ConnectionSlot(k_invalid_socket), std::move(session)));
  client_handler::process_drain_job(static_cast<int>(k_invalid_socket), table,
                                    reactor, scheduler, broker);
  CHECK(table.find(static_cast<int>(k_invalid_socket)) == nullptr);

  reactor.stop();
  broker.shutdown();
}

TEST_CASE("next_decode_deadline_combines_handshake_keepalive_retransmit_and_takeover",
          "[connection]") {
  const BrokerConfig config = make_config();

  auto handshake_connection = std::make_unique<TcpConnection>(k_invalid_socket);
  ConnectionSession handshake_session(std::move(handshake_connection), nullptr,
                                      false, config);
  const auto handshake_deadline = client_handler::next_decode_deadline(handshake_session);
  CHECK(handshake_deadline.has_value());

  auto connected_connection = std::make_unique<TcpConnection>(k_invalid_socket);
  ConnectionSession connected_session(std::move(connected_connection), nullptr,
                                      false, config);
  connected_session.set_phase(ConnectionSession::Phase::Connected);

  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  auto outbound_queue = std::make_shared<OutboundQueue>();
  auto client_session = std::make_unique<ClientSession>(
      "deadline-client", "user", authenticator, outbound_queue,
      inflight_store, 10U, 10U, 8U, std::chrono::milliseconds(50));

  inflight_store.create(
      "deadline-client",
      InflightEntry{.packet_id = 4U,
                    .message = Message{.topic = Utf8String{"d/topic"},
                                       .payload = BinaryData{{0x01U}},
                                       .qos = QoS::AtLeastOnce,
                                       .retain = false,
                                       .properties = {}},
                    .qos = QoS::AtLeastOnce,
                    .state = InflightState::WaitingForPuback,
                    .direction = InflightDirection::Outbound,
                    .timestamp = std::chrono::steady_clock::now()});

  connected_session.install_client_session(std::move(client_session));
  connected_session.arm_session_takeover_close(std::chrono::milliseconds(200));
  const auto connected_deadline =
      client_handler::next_decode_deadline(connected_session);
  CHECK(connected_deadline.has_value());
}

TEST_CASE("process_decode_job_reschedules_when_stream_buffer_still_has_packets",
          "[connection]") {
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
  REQUIRE(set_nonblocking(accepted_socket) == IoResult::Ok);

  auto connection = std::make_unique<TcpConnection>(accepted_socket);
  auto session = std::make_unique<ConnectionSession>(std::move(connection),
                                                     nullptr, false, config);
  session->set_phase(ConnectionSession::Phase::Connected);
  session->connect_result().client_id = "reschedule-client";

  InflightStore inflight_store;
  auto authenticator =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{.status = AuthStatus::Success,
                          .reason_code = ReasonCode::Success,
                          .auth_data = std::nullopt};
      });
  auto outbound_queue = std::make_shared<OutboundQueue>();
  auto client_session = std::make_unique<ClientSession>(
      "reschedule-client", "user", authenticator, outbound_queue,
      inflight_store, 30U, 100U, 8U);
  session->install_client_session(std::move(client_session));

  static constexpr std::array<uint8_t, 2> k_pingreq_frame{0xC0U, 0x00U};
  for (std::size_t index = 0; index < 40U; ++index) {
    session->stream_buffer().append(k_pingreq_frame);
  }

  const int connection_fd = static_cast<int>(accepted_socket);
  REQUIRE(table.add(connection_fd, ConnectionSlot(accepted_socket),
                    std::move(session)));

  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);

  bool decode_job_seen = false;
  const std::size_t pending_jobs = queue.size();
  for (std::size_t index = 0; index < pending_jobs; ++index) {
    const auto maybe_job = queue.pop_blocking();
    REQUIRE(maybe_job.has_value());
    if (maybe_job->type == JobType::Decode) {
      decode_job_seen = true;
    }
  }
  CHECK(decode_job_seen);

  client_handler::process_close_job(connection_fd, table, reactor, broker);
  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}

TEST_CASE("process_decode_job_websocket_without_transport_submits_close",
          "[connection]") {
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
  REQUIRE(set_nonblocking(accepted_socket) == IoResult::Ok);

  auto connection = std::make_unique<TcpConnection>(accepted_socket);
  auto session = std::make_unique<ConnectionSession>(std::move(connection),
                                                     nullptr, true, config);
  const int connection_fd = static_cast<int>(accepted_socket);
  REQUIRE(table.add(connection_fd, ConnectionSlot(accepted_socket),
                    std::move(session)));

  client_handler::process_decode_job(connection_fd, table, reactor, scheduler,
                                     broker);

  const auto maybe_job = queue.pop_blocking();
  REQUIRE(maybe_job.has_value());
  CHECK(maybe_job->type == JobType::Close);

  client_handler::process_close_job(connection_fd, table, reactor, broker);
  close_socket_handle(client_socket);
  reactor.stop();
  broker.shutdown();
#endif
}