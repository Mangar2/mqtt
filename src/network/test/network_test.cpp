#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

#include "network/network_error.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "network/tcp_listener.h"
#include "network/write_queue.h"

using namespace mqtt;

// ────
// Helpers

namespace {

/**
 * @brief Create a connected socket pair using a loopback TCP listener.
 *
 * Works identically on Windows (Winsock2) and POSIX without requiring
 * the non-standard `socketpair()` call.
 *
 * @param out_a Client-side connection.
 * @param out_b Server-side connection (from accept).
 */
void make_socket_pair(TcpConnection &out_a, TcpConnection &out_b) {
  auto listener = TcpListener::listen(0, false);
  uint16_t bound_port = listener.port();

  std::unique_ptr<TcpConnection> accepted;
  std::thread acceptor_thread([&] { accepted = listener.accept(); });

  // Create and connect the client socket using platform API directly
  SocketHandle client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_fd != k_invalid_socket);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bound_port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ret = ::connect(client_fd, reinterpret_cast<const sockaddr *>(&addr),
                      sizeof(addr));
  REQUIRE(ret == 0);

  acceptor_thread.join();
  REQUIRE(accepted != nullptr);

  out_a = TcpConnection{client_fd};
  out_b = std::move(*accepted);
}

/**
 * @brief Build a minimal MQTT packet with the given remaining length value.
 *
 * Uses packet type 0x10 (CONNECT) arbitrarily — only for framing tests.
 * Fills the payload with sequential bytes.
 *
 * @param remaining_length Number of payload bytes.
 * @return Full wire bytes including fixed header.
 */
std::vector<uint8_t> make_packet(uint32_t remaining_length) {
  std::vector<uint8_t> pkt;
  pkt.push_back(0x10); // type byte

  // Encode remaining_length as VBI
  uint32_t val = remaining_length;
  do {
    auto encoded = static_cast<uint8_t>(val & 0x7FU);
    val >>= 7U;
    if (val > 0) {
      encoded |= 0x80U;
    }
    pkt.push_back(encoded);
  } while (val > 0);

  // Payload
  for (uint32_t idx = 0; idx < remaining_length; ++idx) {
    pkt.push_back(static_cast<uint8_t>(idx & 0xFFU));
  }
  return pkt;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// StreamBuffer — Module 6.2

TEST_CASE("stream_buffer_empty_on_construction", "[network]") {
  StreamBuffer buf;
  CHECK(buf.is_empty());
  CHECK_FALSE(buf.has_complete_packet());
}

TEST_CASE("stream_buffer_single_packet_1byte_rl", "[network]") {
  StreamBuffer buf;
  auto pkt = make_packet(1);
  buf.append(pkt);
  CHECK(buf.has_complete_packet());
  auto consumed = buf.consume_packet();
  CHECK(consumed == pkt);
  CHECK(buf.is_empty());
}

TEST_CASE("stream_buffer_single_packet_2byte_rl", "[network]") {
  StreamBuffer buf;
  // Remaining length 128 requires 2-byte VBI encoding
  auto pkt = make_packet(128);
  buf.append(pkt);
  CHECK(buf.has_complete_packet());
  auto consumed = buf.consume_packet();
  CHECK(consumed == pkt);
}

TEST_CASE("stream_buffer_single_packet_3byte_rl", "[network]") {
  StreamBuffer buf;
  // Remaining length 16384 requires 3-byte VBI encoding
  auto pkt = make_packet(16384);
  buf.append(pkt);
  CHECK(buf.has_complete_packet());
  auto consumed = buf.consume_packet();
  CHECK(consumed == pkt);
}

TEST_CASE("stream_buffer_single_packet_4byte_rl", "[network]") {
  StreamBuffer buf;
  // Remaining length 2097152 requires 4-byte VBI encoding
  auto pkt = make_packet(2097152);
  buf.append(pkt);
  CHECK(buf.has_complete_packet());
  auto consumed = buf.consume_packet();
  CHECK(consumed == pkt);
}

TEST_CASE("stream_buffer_fragmented_delivery", "[network]") {
  StreamBuffer buf;
  auto pkt = make_packet(10);

  // Split at index 5
  auto first_half = std::vector<uint8_t>(pkt.begin(), pkt.begin() + 5);
  auto second_half = std::vector<uint8_t>(pkt.begin() + 5, pkt.end());

  buf.append(first_half);
  CHECK_FALSE(buf.has_complete_packet());

  buf.append(second_half);
  CHECK(buf.has_complete_packet());
  CHECK(buf.consume_packet() == pkt);
}

TEST_CASE("stream_buffer_multiple_packets_in_one_append", "[network]") {
  StreamBuffer buf;
  auto pkt1 = make_packet(3);
  auto pkt2 = make_packet(5);

  std::vector<uint8_t> combined;
  combined.insert(combined.end(), pkt1.begin(), pkt1.end());
  combined.insert(combined.end(), pkt2.begin(), pkt2.end());

  buf.append(combined);

  CHECK(buf.has_complete_packet());
  CHECK(buf.consume_packet() == pkt1);

  CHECK(buf.has_complete_packet());
  CHECK(buf.consume_packet() == pkt2);

  CHECK(buf.is_empty());
}

TEST_CASE("stream_buffer_consume_without_complete_packet_throws", "[network]") {
  StreamBuffer buf;
  // Only append the type byte — incomplete packet
  std::array<uint8_t, 1> partial{0x10};
  buf.append(partial);
  CHECK_FALSE(buf.has_complete_packet());
  CHECK_THROWS_AS(buf.consume_packet(), std::logic_error);
}

TEST_CASE("stream_buffer_zero_payload_packet", "[network]") {
  StreamBuffer buf;
  // PINGREQ: 0xC0 0x00 — remaining length = 0
  std::array<uint8_t, 2> pingreq{0xC0, 0x00};
  buf.append(pingreq);
  CHECK(buf.has_complete_packet());
  auto consumed = buf.consume_packet();
  CHECK(consumed.size() == 2);
  CHECK(consumed[0] == 0xC0);
  CHECK(consumed[1] == 0x00);
  CHECK(buf.is_empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteQueue — Module 6.3

TEST_CASE("write_queue_empty_on_construction", "[network]") {
  WriteQueue queue;
  CHECK(queue.is_empty());
  CHECK(queue.queued_bytes() == 0);
  CHECK_FALSE(queue.is_full());
}

TEST_CASE("write_queue_enqueue_increments_bytes", "[network]") {
  WriteQueue queue;
  std::vector<uint8_t> pkt{0x01, 0x02, 0x03};
  CHECK(queue.enqueue(pkt));
  CHECK(queue.queued_bytes() == 3);
  CHECK_FALSE(queue.is_empty());
}

TEST_CASE("write_queue_backpressure_when_full", "[network]") {
  WriteQueue queue(4); // 4-byte capacity
  std::vector<uint8_t> pkt{0x01, 0x02, 0x03, 0x04};
  CHECK(queue.enqueue(pkt)); // exactly at capacity

  // Next enqueue should exceed capacity
  std::vector<uint8_t> extra{0x05};
  CHECK_FALSE(queue.enqueue(extra));
}

TEST_CASE("write_queue_drain_writes_to_connection", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  WriteQueue queue;
  std::vector<uint8_t> pkt = make_packet(2);
  CHECK(queue.enqueue(pkt));
  CHECK(queue.drain(side_a));

  std::vector<uint8_t> received(pkt.size());
  std::ptrdiff_t nread =
      side_b.read(std::span<uint8_t>{received.data(), received.size()});
  CHECK(nread == static_cast<std::ptrdiff_t>(pkt.size()));
  CHECK(received == pkt);
}

TEST_CASE("write_queue_drain_multiple_packets", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  WriteQueue queue;
  auto pkt1 = make_packet(1);
  auto pkt2 = make_packet(2);
  CHECK(queue.enqueue(pkt1));
  CHECK(queue.enqueue(pkt2));
  CHECK(queue.drain(side_a));

  std::size_t total = pkt1.size() + pkt2.size();
  std::vector<uint8_t> received(total);
  std::ptrdiff_t nread =
      side_b.read(std::span<uint8_t>{received.data(), received.size()});
  CHECK(nread == static_cast<std::ptrdiff_t>(total));
  CHECK(queue.is_empty());
}

TEST_CASE("write_queue_is_full_at_capacity", "[network]") {
  WriteQueue queue(3);
  std::vector<uint8_t> pkt{0xAA, 0xBB, 0xCC};
  CHECK(queue.enqueue(pkt));
  CHECK(queue.is_full());
}

TEST_CASE("write_queue_stop_exits_run_drain", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  WriteQueue queue;
  std::thread drain_thread{[&] { queue.run_drain(side_a); }};

  // Give the thread a moment to start and block on the condition variable
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  queue.stop();
  drain_thread.join(); // Must not deadlock
  CHECK(true);         // Reached here → thread exited cleanly
}

TEST_CASE("write_queue_run_drain_writes_enqueued_packets", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  WriteQueue queue;
  auto pkt = make_packet(3);

  std::thread drain_thread{[&] { queue.run_drain(side_a); }};

  CHECK(queue.enqueue(pkt));

  std::vector<uint8_t> received(pkt.size());
  std::ptrdiff_t nread =
      side_b.read(std::span<uint8_t>{received.data(), received.size()});
  CHECK(nread == static_cast<std::ptrdiff_t>(pkt.size()));
  CHECK(received == pkt);

  queue.stop();
  drain_thread.join();
}

// ─────────────────────────────────────────────────────────────────────────────
// TcpConnection — Module 6.1.3

TEST_CASE("tcp_connection_is_open_after_construction", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);
  CHECK(side_a.is_open());
  CHECK(side_b.is_open());
}

TEST_CASE("tcp_connection_is_closed_after_close", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);
  side_a.close();
  CHECK_FALSE(side_a.is_open());
}

TEST_CASE("tcp_connection_read_write_roundtrip", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  std::array<uint8_t, 3> data{0xDE, 0xAD, 0xBE};
  CHECK(side_a.write(data));

  std::array<uint8_t, 3> recv{};
  std::ptrdiff_t nread = side_b.read(recv);
  CHECK(nread == 3);
  CHECK(recv == data);
}

TEST_CASE("tcp_connection_read_returns_zero_on_peer_close", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  side_a.close();

  std::array<uint8_t, 4> buf{};
  std::ptrdiff_t nread = side_b.read(buf);
  CHECK(nread == 0);
}

TEST_CASE("tcp_connection_write_returns_false_on_closed_socket", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  side_b.close();
  side_a.close(); // close side_a too

  std::array<uint8_t, 2> data{0x01, 0x02};
  CHECK_FALSE(side_a.write(data));
}

TEST_CASE("tcp_connection_move_transfers_ownership", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);

  auto original_fd = side_a.fd();
  TcpConnection moved = std::move(side_a);

  CHECK_FALSE(side_a.is_open()); // NOLINT(bugprone-use-after-move)
  CHECK(moved.is_open());
  CHECK(moved.fd() == original_fd);
}

TEST_CASE("tcp_connection_fd_returns_valid_descriptor", "[network]") {
  TcpConnection side_a{k_invalid_socket};
  TcpConnection side_b{k_invalid_socket};
  make_socket_pair(side_a, side_b);
  CHECK(side_a.is_open());
  CHECK(side_a.fd() != k_invalid_socket);
}

// ─────────────────────────────────────────────────────────────────────────────
// TcpListener — Module 6.1.1–6.1.2

TEST_CASE("tcp_listener_opens_on_available_port", "[network]") {
  auto listener = TcpListener::listen(0);
  CHECK(listener.is_open());
}

TEST_CASE("tcp_listener_port_returns_bound_port", "[network]") {
  auto listener = TcpListener::listen(0);
  uint16_t bound = listener.port();
  CHECK(bound != 0);
}

TEST_CASE("tcp_listener_accept_returns_connection", "[network]") {
  auto listener = TcpListener::listen(0);
  uint16_t bound = listener.port();

  // Connect from a client thread
  std::unique_ptr<TcpConnection> server_conn;
  std::thread acceptor{[&] { server_conn = listener.accept(); }};

  // Client connects via platform socket API
  SocketHandle client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_fd != k_invalid_socket);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bound);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ret =
      ::connect(client_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  CHECK(ret == 0);

  acceptor.join();
  REQUIRE(server_conn != nullptr);
  CHECK(server_conn->is_open());

#ifdef _WIN32
  ::closesocket(client_fd);
#else
  ::close(client_fd);
#endif
}

TEST_CASE("tcp_listener_close_marks_as_closed", "[network]") {
  auto listener = TcpListener::listen(0);
  listener.close();
  CHECK_FALSE(listener.is_open());
}

TEST_CASE("tcp_listener_move_transfers_ownership", "[network]") {
  auto listener = TcpListener::listen(0);
  CHECK(listener.is_open());

  auto moved = std::move(listener);
  CHECK_FALSE(listener.is_open()); // NOLINT(bugprone-use-after-move)
  CHECK(moved.is_open());
}
TEST_CASE("tcp_listener_move_assignment_transfers_ownership", "[network]") {
  auto listener1 = TcpListener::listen(0);
  auto listener2 = TcpListener::listen(0);
  CHECK(listener1.is_open());
  CHECK(listener2.is_open());

  listener2 = std::move(listener1);
  CHECK_FALSE(listener1.is_open()); // NOLINT(bugprone-use-after-move)
  CHECK(listener2.is_open());
}

TEST_CASE("tcp_listener_ipv6_opens_and_reports_port", "[network]") {
  auto listener = TcpListener::listen(0, true);
  REQUIRE(listener.is_open());
  CHECK(listener.port() != 0);
}

TEST_CASE("tcp_listener_accept_on_closed_throws", "[network]") {
  auto listener = TcpListener::listen(0);
  listener.close();
  CHECK_THROWS_AS((void)listener.accept(), NetworkException);
}

TEST_CASE("tcp_listener_port_on_closed_returns_zero", "[network]") {
  auto listener = TcpListener::listen(0);
  listener.close();
  CHECK(listener.port() == 0);
}

#ifdef _WIN32
// Force bind() failure by holding the port with SO_EXCLUSIVEADDRUSE.
// SO_EXCLUSIVEADDRUSE prevents any other socket from binding to the same
// port, even one with SO_REUSEADDR — exercising the BindFailed error path.

TEST_CASE("tcp_listener_ipv4_bind_failure_throws", "[network]") {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  SOCKET blocker = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(blocker != INVALID_SOCKET);

  BOOL exclusive = TRUE;
  ::setsockopt(blocker, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = INADDR_ANY;
  REQUIRE(::bind(blocker, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
          0);
  ::listen(blocker, 1);

  int len = sizeof(addr);
  ::getsockname(blocker, reinterpret_cast<sockaddr *>(&addr), &len);
  uint16_t locked_port = ntohs(addr.sin_port);

  CHECK_THROWS_AS(TcpListener::listen(locked_port), NetworkException);
  ::closesocket(blocker);
}

TEST_CASE("tcp_listener_ipv6_bind_failure_throws", "[network]") {
  WSADATA wsa{};
  WSAStartup(MAKEWORD(2, 2), &wsa);

  SOCKET blocker = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(blocker != INVALID_SOCKET);

  BOOL exclusive = TRUE;
  ::setsockopt(blocker, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));

  // Match the dual-stack mode our TcpListener uses (IPV6_V6ONLY=0) so the OS
  // treats blocker and listener as bound to the same address.
  DWORD v6only = 0;
  ::setsockopt(blocker, IPPROTO_IPV6, IPV6_V6ONLY,
               reinterpret_cast<const char *>(&v6only), sizeof(v6only));

  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = 0;
  addr.sin6_addr = in6addr_any;
  REQUIRE(::bind(blocker, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
          0);
  ::listen(blocker, 1);

  int len = sizeof(addr);
  ::getsockname(blocker, reinterpret_cast<sockaddr *>(&addr), &len);
  uint16_t locked_port = ntohs(addr.sin6_port);

  CHECK_THROWS_AS(TcpListener::listen(locked_port, true), NetworkException);
  ::closesocket(blocker);
}
#endif // _WIN32

// ─────────────────────────────────────────────────────────────────────────────
// NetworkException — network_error.h

TEST_CASE("network_exception_stores_code_and_message", "[network]") {
  NetworkException exc{NetworkError::BindFailed, "bind failed on port 80"};
  CHECK(exc.code() == NetworkError::BindFailed);
  CHECK(std::string_view{exc.what()} == "bind failed on port 80");
}

TEST_CASE("network_exception_all_error_codes_constructible", "[network]") {
  // Verify each error code round-trips through the exception type.
  auto check = [](NetworkError code) {
    NetworkException exc{code, "test"};
    CHECK(exc.code() == code);
  };
  check(NetworkError::SocketCreateFailed);
  check(NetworkError::BindFailed);
  check(NetworkError::ListenFailed);
  check(NetworkError::AcceptFailed);
  check(NetworkError::SetSockOptFailed);
  check(NetworkError::WriteFailed);
  check(NetworkError::ReadFailed);
  check(NetworkError::QueueFull);
}