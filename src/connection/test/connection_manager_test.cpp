#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <vector>
#include <thread>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "connection/connection_manager.h"
#include "network/tcp_listener.h"

using namespace mqtt;

namespace {

uint16_t allocate_unused_port() {
  TcpListener listener = TcpListener::listen(0U);
  const uint16_t port_value = listener.port();
  listener.close();
  return port_value;
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
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

SocketHandle connect_loopback_with_retry(uint16_t port_value,
                                         std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    SocketHandle socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(socket_handle != k_invalid_socket);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_value);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const int connect_result =
        ::connect(socket_handle, reinterpret_cast<const sockaddr *>(&server_addr),
                  sizeof(server_addr));
    if (connect_result == 0) {
      return socket_handle;
    }

#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(socket_handle));
#else
    ::close(static_cast<int>(socket_handle));
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return k_invalid_socket;
}

void close_socket_handle(SocketHandle socket_handle) {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

BrokerConfig make_config() {
  BrokerConfig config;
  config.mqtt_port = 0U;
  config.ws_port = 0U;
  config.allow_anonymous = true;
  config.persistence_mode = PersistenceMode::Off;
  return config;
}

} // namespace

TEST_CASE("connection_manager_start_stop_without_callback_api", "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t mqtt_port = allocate_unused_port();
  ConnectionManager manager(mqtt_port, 0U, broker, config);

  manager.start();
  CHECK(manager.is_running());

  manager.stop();
  CHECK_FALSE(manager.is_running());

  CHECK_NOTHROW(manager.stop());
}

TEST_CASE("connection_manager_rejects_same_ports", "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t shared_port = allocate_unused_port();
  ConnectionManager manager(shared_port, shared_port, broker, config);

  CHECK_THROWS(manager.start());
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_accept_path_mqtt_and_ws", "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t mqtt_port = allocate_unused_port();
  const uint16_t ws_port = allocate_unused_port();
  ConnectionManager manager(mqtt_port, ws_port, broker, config);

  manager.start();
  connect_loopback(mqtt_port);
  connect_loopback(ws_port);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  manager.stop();
  CHECK_FALSE(manager.is_running());
}

TEST_CASE("connection_manager_start_idempotent", "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t mqtt_port = allocate_unused_port();
  ConnectionManager manager(mqtt_port, 0U, broker, config);

  manager.start();
  manager.start();
  CHECK(manager.is_running());

  manager.stop();
}

TEST_CASE("connection_manager_ws_bind_failure_after_mqtt_listener",
          "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t mqtt_port = allocate_unused_port();
  TcpListener occupied_ws_listener = TcpListener::listen(0U);
  const uint16_t ws_port = occupied_ws_listener.port();

  ConnectionManager manager(mqtt_port, ws_port, broker, config);
  CHECK_THROWS(manager.start());
  CHECK_FALSE(manager.is_running());

  occupied_ws_listener.close();
}

TEST_CASE("connection_manager_stop_with_active_mqtt_connection",
          "[connection]") {
  BrokerConfig config = make_config();
  Broker broker(config);

  const uint16_t mqtt_port = allocate_unused_port();
  ConnectionManager manager(mqtt_port, 0U, broker, config);

  manager.start();
  CHECK(manager.is_running());

  const SocketHandle mqtt_client =
      connect_loopback_with_retry(mqtt_port, std::chrono::milliseconds(500));
  REQUIRE(mqtt_client != k_invalid_socket);

  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  manager.stop();
  CHECK_FALSE(manager.is_running());

  close_socket_handle(mqtt_client);
}

