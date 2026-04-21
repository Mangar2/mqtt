#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "connection/connection_manager.h"
#include "network/tcp_connection.h"
#include "network/tcp_listener.h"

using namespace mqtt;
using namespace std::chrono_literals;

namespace mqtt {

struct ConnectionManagerTestAccessor {
  static std::string io_result_to_string(IoResult io_result) {
    return ConnectionManager::io_result_to_string_for_test(io_result);
  }

  static bool set_socket_blocking(SocketHandle socket_handle) {
    return ConnectionManager::set_socket_blocking_for_test(socket_handle);
  }

  static void close_socket_handle(SocketHandle socket_handle) {
    ConnectionManager::close_socket_handle_for_test(socket_handle);
  }

  static void set_running(ConnectionManager &manager, bool running) {
    manager.running_.store(running);
  }

  static void handle_accept_ready(ConnectionManager &manager,
                                  SocketHandle listener_socket_handle,
                                  bool is_ws) {
    manager.handle_accept_ready(listener_socket_handle, is_ws);
  }
};

} // namespace mqtt

namespace {

void close_socket_handle(SocketHandle socket_handle) {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

void connect_loopback(uint16_t port_value) {
  SocketHandle socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(socket_handle != k_invalid_socket);

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_value);
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const int connect_result =
      ::connect(socket_handle, reinterpret_cast<const sockaddr *>(&server_addr),
                sizeof(server_addr));
  REQUIRE(connect_result == 0);
  close_socket_handle(socket_handle);
}

uint16_t allocate_unused_port() {
  TcpListener listener = TcpListener::listen(0U);
  const uint16_t port_value = listener.port();
  listener.close();
  return port_value;
}

bool wait_for_true(const std::atomic<bool> &flag,
                   std::chrono::milliseconds timeout_value) {
  const auto deadline = std::chrono::steady_clock::now() + timeout_value;
  while (std::chrono::steady_clock::now() < deadline) {
    if (flag.load()) {
      return true;
    }
    std::this_thread::sleep_for(10ms);
  }
  return flag.load();
}

} // namespace

TEST_CASE("connection_manager_start_stop_mqtt", "[connection]") {
  const uint16_t mqtt_port = allocate_unused_port();
  std::atomic<bool> handled{false};

  ConnectionManager manager(
      mqtt_port, 0U,
      [&handled](std::unique_ptr<TcpConnection> connection,
                 bool is_ws) {
        (void)is_ws;
        if (connection) {
          connection->close();
        }
        handled.store(true);
      });

  manager.start();
  CHECK(manager.is_running());

  connect_loopback(mqtt_port);
  CHECK(wait_for_true(handled, 500ms));

  manager.stop();
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_start_stop_ws", "[connection]") {
  const uint16_t ws_port = allocate_unused_port();
  std::atomic<bool> handled_ws{false};

  ConnectionManager manager(
      0U, ws_port,
      [&handled_ws](std::unique_ptr<TcpConnection> connection,
                    bool is_ws) {
        if (connection) {
          connection->close();
        }
        if (is_ws) {
          handled_ws.store(true);
        }
      });

  manager.start();
  CHECK(manager.is_running());

  connect_loopback(ws_port);
  CHECK(wait_for_true(handled_ws, 500ms));

  manager.stop();
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_stop_without_start", "[connection]") {
  ConnectionManager manager(
      0U, 0U,
      [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)connection;
        (void)is_ws;
      });

  CHECK_FALSE(manager.is_running());
  CHECK_NOTHROW(manager.stop());
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_start_idempotent", "[connection]") {
  const uint16_t mqtt_port = allocate_unused_port();
  std::atomic<bool> handled{false};

  ConnectionManager manager(
      mqtt_port, 0U,
      [&handled](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)is_ws;
        if (connection) {
          connection->close();
        }
        handled.store(true);
      });

  manager.start();
  manager.start();
  CHECK(manager.is_running());

  connect_loopback(mqtt_port);
  CHECK(wait_for_true(handled, 500ms));

  manager.stop();
}

TEST_CASE("connection_manager_start_failure_resets_running", "[connection]") {
  const uint16_t shared_port = allocate_unused_port();

  ConnectionManager manager(
      shared_port, shared_port,
      [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)connection;
        (void)is_ws;
      });

  CHECK_THROWS(manager.start());
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_start_bind_failure_inside_try_resets_running",
          "[connection]") {
  TcpListener occupied_listener = TcpListener::listen(0U);
  const uint16_t occupied_port = occupied_listener.port();

  ConnectionManager manager(
      occupied_port, 0U,
      [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)connection;
        (void)is_ws;
      });

  CHECK_THROWS(manager.start());
  CHECK_FALSE(manager.is_running());

  occupied_listener.close();
}

TEST_CASE("connection_manager_start_ws_bind_failure_after_mqtt_listener_cleans_"
          "mqtt_listener",
          "[connection]") {
  const uint16_t mqtt_port = allocate_unused_port();
  TcpListener occupied_ws_listener = TcpListener::listen(0U);
  const uint16_t ws_port = occupied_ws_listener.port();

  ConnectionManager manager(
      mqtt_port, ws_port,
      [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)connection;
        (void)is_ws;
      });

  CHECK_THROWS(manager.start());
  CHECK_FALSE(manager.is_running());

  occupied_ws_listener.close();
}

TEST_CASE("connection_manager_stop_timeout_requests_socket_shutdown",
          "[connection]") {
  const uint16_t mqtt_port = allocate_unused_port();
  std::atomic<bool> entered_callback{false};

  ConnectionManager manager(
      mqtt_port, 0U,
      [&entered_callback](std::unique_ptr<TcpConnection> connection,
                          bool is_ws) {
        (void)is_ws;
        entered_callback.store(true);
        std::this_thread::sleep_for(120ms);
        if (connection) {
          connection->close();
        }
      },
      std::chrono::milliseconds(20));

  manager.start();
  connect_loopback(mqtt_port);
  CHECK(wait_for_true(entered_callback, 300ms));

  CHECK_NOTHROW(manager.stop());
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_callback_exception_isolated", "[connection]") {
  const uint16_t mqtt_port = allocate_unused_port();

  ConnectionManager manager(
      mqtt_port, 0U,
      [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
        (void)connection;
        (void)is_ws;
        throw std::runtime_error("expected callback failure");
      });

  manager.start();
  connect_loopback(mqtt_port);
  std::this_thread::sleep_for(30ms);

  CHECK_NOTHROW(manager.stop());
}

  TEST_CASE("connection_manager_io_result_to_string_covers_all_cases",
        "[connection]") {
    CHECK(ConnectionManagerTestAccessor::io_result_to_string(IoResult::Ok) == "ok");
    CHECK(ConnectionManagerTestAccessor::io_result_to_string(IoResult::WouldBlock) ==
      "would_block");
    CHECK(ConnectionManagerTestAccessor::io_result_to_string(IoResult::Closed) ==
      "closed");
    CHECK(ConnectionManagerTestAccessor::io_result_to_string(IoResult::Error) ==
      "error");
    CHECK(ConnectionManagerTestAccessor::io_result_to_string(
      static_cast<IoResult>(255)) == "unknown");
  }

  TEST_CASE("connection_manager_set_socket_blocking_for_test_invalid_handle_returns_"
        "false",
        "[connection]") {
    CHECK_FALSE(
    ConnectionManagerTestAccessor::set_socket_blocking(k_invalid_socket));
  }

  TEST_CASE("connection_manager_close_socket_handle_for_test_invalid_handle_no_throw",
        "[connection]") {
    CHECK_NOTHROW(
    ConnectionManagerTestAccessor::close_socket_handle(k_invalid_socket));
  }

  TEST_CASE("connection_manager_handle_accept_ready_invalid_listener_enters_error_"
        "path",
        "[connection]") {
    ConnectionManager manager(
    0U, 0U,
    [](std::unique_ptr<TcpConnection> connection, bool is_ws) {
      (void)connection;
      (void)is_ws;
    });

    ConnectionManagerTestAccessor::set_running(manager, true);
    CHECK_NOTHROW(ConnectionManagerTestAccessor::handle_accept_ready(
    manager, k_invalid_socket, false));
    ConnectionManagerTestAccessor::set_running(manager, false);
  }
