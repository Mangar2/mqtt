#include <catch2/catch_test_macros.hpp>

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"

using namespace mqtt;
using namespace std::chrono_literals;

namespace {

void close_fd(int socket_fd) noexcept {
  if (socket_fd >= 0) {
    ::close(socket_fd);
  }
}

bool wait_for_condition(const std::function<bool()> &predicate,
                        std::chrono::milliseconds timeout_value) {
  const auto deadline = std::chrono::steady_clock::now() + timeout_value;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return predicate();
}

int create_nonblocking_listener() {
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

uint16_t query_listener_port(int listener_fd) {
  sockaddr_in bound_address{};
  socklen_t bound_address_size = sizeof(bound_address);
  REQUIRE(::getsockname(listener_fd, reinterpret_cast<sockaddr *>(&bound_address),
                        &bound_address_size) == 0);
  return ntohs(bound_address.sin_port);
}

void connect_loopback(uint16_t port_value) {
  const int client_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_fd >= 0);

  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port_value);
  server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  REQUIRE(::connect(client_fd, reinterpret_cast<const sockaddr *>(&server_address),
                    sizeof(server_address)) == 0);
  close_fd(client_fd);
}

void drain_nonblocking_read(int socket_fd) {
  std::array<uint8_t, 256> read_buffer{};
  while (true) {
    std::size_t bytes_read = 0U;
    const IoResult result =
        nb_read(static_cast<SocketHandle>(socket_fd), read_buffer, &bytes_read);
    if (result != IoResult::Ok || bytes_read == 0U) {
      break;
    }
  }
}

} // namespace

TEST_CASE("start_then_stop_creates_and_joins_reactor_threads", "[network]") {
  IoReactor reactor;
  REQUIRE_NOTHROW(reactor.start());
  REQUIRE_NOTHROW(reactor.stop());
}

TEST_CASE("register_listener_invokes_accept_callback_on_incoming_connection",
          "[network]") {
  IoReactor reactor;
  reactor.start();

  const int listener_fd = create_nonblocking_listener();
  const uint16_t listener_port = query_listener_port(listener_fd);
  std::atomic<int> accepted_count{0};

  reactor.register_listener(listener_fd, [&accepted_count](int active_listener_fd) {
    while (true) {
      SocketHandle accepted_socket = k_invalid_socket;
      const IoResult accept_result =
          nb_accept(static_cast<SocketHandle>(active_listener_fd), &accepted_socket);
      if (accept_result == IoResult::WouldBlock) {
        break;
      }
      if (accept_result != IoResult::Ok || accepted_socket == k_invalid_socket) {
        break;
      }
      accepted_count.fetch_add(1);
      close_fd(static_cast<int>(accepted_socket));
    }
  });

  connect_loopback(listener_port);
  CHECK(wait_for_condition([&accepted_count]() { return accepted_count.load() >= 1; },
                           500ms));

  reactor.unregister(listener_fd);
  reactor.stop();
  close_fd(listener_fd);
}

TEST_CASE("register_connection_invokes_read_callback_when_data_arrives",
          "[network]") {
  IoReactor reactor;
  reactor.start();

  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  std::atomic<int> read_callback_count{0};
  reactor.register_connection(
      sockets[0],
      [&read_callback_count](int socket_fd) {
        read_callback_count.fetch_add(1);
        drain_nonblocking_read(socket_fd);
      },
      [](int socket_fd) { (void)socket_fd; });

  const std::array<uint8_t, 3> payload{0x10, 0x20, 0x30};
  REQUIRE(::write(sockets[1], payload.data(), payload.size()) ==
          static_cast<std::ptrdiff_t>(payload.size()));

  CHECK(wait_for_condition(
      [&read_callback_count]() { return read_callback_count.load() >= 1; }, 500ms));

  reactor.unregister(sockets[0]);
  reactor.stop();
  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("arm_write_invokes_write_callback_when_socket_writable", "[network]") {
  IoReactor reactor;
  reactor.start();

  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  std::atomic<int> write_callback_count{0};
  reactor.register_connection(
      sockets[0], [](int socket_fd) { (void)socket_fd; },
      [&write_callback_count](int socket_fd) {
        (void)socket_fd;
        write_callback_count.fetch_add(1);
      });

  reactor.arm_write(sockets[0]);
  CHECK(wait_for_condition(
      [&write_callback_count]() { return write_callback_count.load() >= 1; }, 500ms));

  reactor.disarm_write(sockets[0]);
  reactor.unregister(sockets[0]);
  reactor.stop();
  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("unregister_stops_callbacks_for_fd", "[network]") {
  IoReactor reactor;
  reactor.start();

  int sockets[2]{-1, -1};
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  REQUIRE(set_nonblocking(static_cast<SocketHandle>(sockets[0])) == IoResult::Ok);

  std::atomic<int> read_callback_count{0};
  reactor.register_connection(
      sockets[0],
      [&read_callback_count](int socket_fd) {
        read_callback_count.fetch_add(1);
        drain_nonblocking_read(socket_fd);
      },
      [](int socket_fd) { (void)socket_fd; });
  reactor.unregister(sockets[0]);

  const std::array<uint8_t, 2> payload{0x01, 0x02};
  REQUIRE(::write(sockets[1], payload.data(), payload.size()) ==
          static_cast<std::ptrdiff_t>(payload.size()));

  std::this_thread::sleep_for(50ms);
  CHECK(read_callback_count.load() == 0);

  reactor.stop();
  close_fd(sockets[0]);
  close_fd(sockets[1]);
}

TEST_CASE("concurrent_register_unregister_does_not_drop_events", "[network]") {
  IoReactor reactor;
  reactor.start();

  constexpr int socket_pair_count = 100;
  std::vector<std::array<int, 2>> socket_pairs(static_cast<std::size_t>(socket_pair_count));
  std::unordered_map<int, int> fd_to_index;
  fd_to_index.reserve(static_cast<std::size_t>(socket_pair_count));

  for (int index = 0; index < socket_pair_count; ++index) {
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pairs[index].data()) == 0);
    REQUIRE(set_nonblocking(static_cast<SocketHandle>(socket_pairs[index][0])) ==
            IoResult::Ok);
    fd_to_index.emplace(socket_pairs[index][0], index);
  }

  std::vector<std::atomic<bool>> seen(
      static_cast<std::size_t>(socket_pair_count));
  for (auto &seen_flag : seen) {
    seen_flag.store(false);
  }
  std::atomic<int> event_count{0};

  std::thread register_thread([&reactor, &socket_pairs, &fd_to_index, &seen,
                               &event_count]() {
    for (const auto &pair : socket_pairs) {
      reactor.register_connection(
          pair[0],
          [&fd_to_index, &seen, &event_count](int socket_fd) {
            const int index = fd_to_index.at(socket_fd);
            if (!seen[static_cast<std::size_t>(index)].exchange(true)) {
              event_count.fetch_add(1);
            }
            drain_nonblocking_read(socket_fd);
          },
          [](int socket_fd) { (void)socket_fd; });
    }
  });

  std::thread writer_thread([&socket_pairs]() {
    const uint8_t byte = 0x7F;
    for (const auto &pair : socket_pairs) {
      (void)::write(pair[1], &byte, sizeof(byte));
    }
  });

  register_thread.join();
  writer_thread.join();

  CHECK(wait_for_condition(
      [&event_count]() { return event_count.load() == socket_pair_count; }, 1000ms));

  std::thread unregister_thread([&reactor, &socket_pairs]() {
    for (const auto &pair : socket_pairs) {
      reactor.unregister(pair[0]);
    }
  });
  unregister_thread.join();

  reactor.stop();
  for (const auto &pair : socket_pairs) {
    close_fd(pair[0]);
    close_fd(pair[1]);
  }
}

#endif
