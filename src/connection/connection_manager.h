#pragma once

/**
 * @file connection_manager.h
 * @brief ConnectionManager for listener and client-thread lifecycle (Module 23).
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"
#include "network/tcp_listener.h"

namespace mqtt {

class StructuredTracer;
struct ConnectionManagerTestAccessor;

/**
 * @brief Manages listener sockets, reactor lifecycle, and tracked client threads.
 *
 * This class owns the networking lifecycle that was previously embedded in
 * `Broker`. It starts one IoReactor and registers enabled listeners. Accepted
 * connections are still dispatched to tracked per-client threads (bridge mode).
 */
class ConnectionManager {
public:
  /**
   * @brief Callback signature for handing over an accepted client connection.
   */
  using ClientHandlerCallback =
      std::function<void(std::unique_ptr<TcpConnection>, bool is_ws)>;

  /**
   * @brief Construct a manager for MQTT and optional WebSocket listeners.
   *
   * @param mqtt_port MQTT listener port. `0` disables the MQTT listener.
   * @param ws_port WebSocket listener port. `0` disables the WS listener.
   * @param callback Callback invoked on each accepted client connection.
   * @param client_join_timeout Timeout used by `stop()` while waiting for
   *                            client threads to finish before forcing socket
   *                            shutdown.
   */
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port,
                    ClientHandlerCallback callback,
                    std::chrono::milliseconds client_join_timeout =
                        std::chrono::seconds(2),
                    StructuredTracer *structured_tracer = nullptr);

  /** @brief Destructor that performs best-effort shutdown via `stop()`. */
  ~ConnectionManager();

  ConnectionManager(const ConnectionManager &) = delete;
  ConnectionManager &operator=(const ConnectionManager &) = delete;

  /**
   * @brief Start all configured listeners and the IoReactor.
   *
   * Throws on socket/listener failures and leaves the manager stopped.
   */
  void start();

  /**
   * @brief Stop reactor/listeners and terminate client threads.
   *
   * This method is idempotent and never throws.
   */
  void stop() noexcept;

  /**
   * @brief Return whether the manager is running.
   * @return `true` after successful `start()` until `stop()`.
   */
  [[nodiscard]] bool is_running() const noexcept;

private:
  friend struct ConnectionManagerTestAccessor;

  [[nodiscard]] static std::string io_result_to_string_for_test(IoResult io_result);
  [[nodiscard]] static bool set_socket_blocking_for_test(
      SocketHandle socket_handle) noexcept;
  static void close_socket_handle_for_test(SocketHandle socket_handle) noexcept;

  /**
   * @brief Tracked client thread metadata.
   */
  struct ClientThreadEntry {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> finished;
    SocketHandle socket_handle{0U};
  };

  /** @brief Handle one listener-ready event and drain pending accepts. */
  void handle_accept_ready(SocketHandle listener_socket_handle, bool is_ws);

  /** @brief Join and remove finished client threads from the registry. */
  void cleanup_finished();

  /** @brief Best-effort join of all client threads. */
  void join_all_clients() noexcept;

  uint16_t mqtt_port_{0U};
  uint16_t ws_port_{0U};
  ClientHandlerCallback callback_;

  std::chrono::milliseconds client_join_timeout_;

  mutable std::mutex lifecycle_mutex_;
  std::atomic<bool> running_{false};
  StructuredTracer *structured_tracer_{nullptr};

  std::unique_ptr<IoReactor> io_reactor_;
  ConnectionTable connection_table_;

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;

  std::mutex client_threads_mutex_;
  std::vector<ClientThreadEntry> client_threads_;
};

} // namespace mqtt
