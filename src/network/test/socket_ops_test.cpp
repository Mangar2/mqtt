#include <catch2/catch_test_macros.hpp>

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <chrono>
#include <thread>

#include "network/socket_ops.h"
#include "network/tcp_connection.h"

using namespace mqtt;

namespace {

void close_fd(int socket_fd) noexcept {
  if (socket_fd >= 0) {
    ::close(socket_fd);
  }
}

[[nodiscard]] int create_nonblocking_listener() {
  const int listener_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(listener_fd >= 0);

  int reuse_address = 1;
  REQUIRE(::setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                       sizeof(reuse_address)) == 0);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = 0;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  REQUIRE(::bind(listener_fd, reinterpret_cast<const sockaddr *>(&address),
                 sizeof(address)) == 0);
  REQUIRE(::listen(listener_fd, 16) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(listener_fd)) ==
          IoResult::Ok);
  return listener_fd;
}

[[nodiscard]] uint16_t query_listener_port(int listener_fd) {
  sockaddr_in bound_address{};
  socklen_t bound_address_size = sizeof(bound_address);
  REQUIRE(::getsockname(listener_fd, reinterpret_cast<sockaddr *>(&bound_address),
                        &bound_address_size) == 0);
  return ntohs(bound_address.sin_port);
}

} // namespace

TEST_CASE("set_nonblocking_returns_ok_on_valid_fd", "[network]") {
  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);
  const int current_flags = ::fcntl(sockets[0], F_GETFL, 0);
  REQUIRE(current_flags >= 0);
  CHECK((current_flags & O_NONBLOCK) != 0);

  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("nb_read_returns_would_block_on_empty_socket", "[network]") {
  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  std::array<uint8_t, 32> read_buffer{};
  std::size_t bytes_read = 123U;
  const IoResult io_result =
      nb_read(static_cast<SocketHandle>(sockets[0]), read_buffer, &bytes_read);
  CHECK(io_result == IoResult::WouldBlock);
  CHECK(bytes_read == 0U);

  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("nb_read_returns_ok_with_bytes_when_data_present", "[network]") {
  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  const std::array<uint8_t, 4> payload{0xAA, 0xBB, 0xCC, 0xDD};
  REQUIRE(::write(sockets[1], payload.data(), payload.size()) ==
          static_cast<std::ptrdiff_t>(payload.size()));

  std::array<uint8_t, 16> read_buffer{};
  std::size_t bytes_read = 0U;
  const IoResult io_result =
      nb_read(static_cast<SocketHandle>(sockets[0]), read_buffer, &bytes_read);
  REQUIRE(io_result == IoResult::Ok);
  REQUIRE(bytes_read == payload.size());
  CHECK(std::equal(payload.begin(), payload.end(), read_buffer.begin()));

  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("nb_read_returns_closed_on_peer_shutdown", "[network]") {
  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  close_fd(sockets[1]);
  sockets[1] = -1;

  std::array<uint8_t, 8> read_buffer{};
  CHECK(nb_read(static_cast<SocketHandle>(sockets[0]), read_buffer) ==
        IoResult::Closed);

  close_fd(sockets[0]);
}

TEST_CASE("nb_write_returns_would_block_when_buffer_full", "[network]") {
  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  int send_buffer_size = 1024;
  REQUIRE(::setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &send_buffer_size,
                       sizeof(send_buffer_size)) == 0);

  const std::vector<uint8_t> chunk(4096U, 0x2A);
  bool saw_would_block = false;

  for (int attempt_index = 0; attempt_index < 200000; ++attempt_index) {
    std::size_t bytes_written = 0U;
    const IoResult io_result =
        nb_write(static_cast<SocketHandle>(sockets[0]), chunk, &bytes_written);
    if (io_result == IoResult::WouldBlock) {
      saw_would_block = true;
      break;
    }
    REQUIRE(io_result == IoResult::Ok);
    REQUIRE(bytes_written > 0U);
  }

  CHECK(saw_would_block);
  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("nb_accept_returns_would_block_when_no_pending", "[network]") {
  const int listener_fd = create_nonblocking_listener();
  SocketHandle accepted_socket = k_invalid_socket;
  const IoResult io_result =
      nb_accept(static_cast<SocketHandle>(listener_fd), &accepted_socket);
  CHECK(io_result == IoResult::WouldBlock);
  CHECK(accepted_socket == k_invalid_socket);
  close_fd(listener_fd);
}

TEST_CASE("nb_accept_returns_ok_when_client_pending", "[network]") {
  const int listener_fd = create_nonblocking_listener();
  const uint16_t listen_port = query_listener_port(listener_fd);

  const int client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_fd >= 0);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(listen_port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  REQUIRE(::connect(client_fd, reinterpret_cast<const sockaddr *>(&address),
                    sizeof(address)) == 0);

  SocketHandle accepted_socket = k_invalid_socket;
  IoResult accept_result = IoResult::WouldBlock;
  for (int attempt_index = 0; attempt_index < 200; ++attempt_index) {
    accept_result =
        nb_accept(static_cast<SocketHandle>(listener_fd), &accepted_socket);
    if (accept_result == IoResult::Ok) {
      break;
    }
    REQUIRE(accept_result == IoResult::WouldBlock);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  REQUIRE(accept_result == IoResult::Ok);
  REQUIRE(accepted_socket != k_invalid_socket);

  close_fd(static_cast<int>(accepted_socket));
  close_fd(client_fd);
  close_fd(listener_fd);
}

#endif
