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
#include <vector>

#include "executor/worker_pool.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"
#include "network/tcp_listener.h"

namespace mqtt {

class Broker;
struct BrokerConfig;
class StructuredTracer;
struct ConnectionManagerTestAccessor;

/**
 * @brief Manages listener sockets, reactor lifecycle, and tracked client threads.
 *
 * This class owns the networking lifecycle that was previously embedded in
 * `Broker`. It starts one IoReactor and registers enabled listeners. Accepted
 * connections are dispatched as WorkerPool jobs with bounded thread count.
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
   * @param worker_stop_timeout Timeout used by `stop()` while waiting for
   *                            worker jobs to drain after socket shutdown.
   * @param worker_min_threads Minimum worker pool startup threads.
   * @param worker_max_threads Maximum worker pool thread cap (0 = default).
   */
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port,
                    ClientHandlerCallback callback,
                    std::chrono::milliseconds worker_stop_timeout =
                        std::chrono::seconds(2),
                    std::size_t worker_min_threads = 2U,
                    std::size_t worker_max_threads = 0U,
                    StructuredTracer *structured_tracer = nullptr);

  /**
   * @brief Backward-compatible constructor using default worker limits.
   */
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port,
                    ClientHandlerCallback callback,
                    std::chrono::milliseconds worker_stop_timeout,
                    StructuredTracer *structured_tracer);

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

  /**
   * @brief Bind broker runtime required by step-based worker job handlers.
   */
  void bind_runtime(Broker &broker, const BrokerConfig &config) noexcept;

private:
  friend struct ConnectionManagerTestAccessor;

  [[nodiscard]] static std::string io_result_to_string_for_test(IoResult io_result);
  [[nodiscard]] static bool set_socket_blocking_for_test(
      SocketHandle socket_handle) noexcept;
  static void close_socket_handle_for_test(SocketHandle socket_handle) noexcept;

  /** @brief Handle one listener-ready event and drain pending accepts. */
  void handle_accept_ready(SocketHandle listener_socket_handle, bool is_ws);

  /** @brief Execute one connection job from the worker pool. */
  void handle_connection_job(const ConnectionJob &job);

  uint16_t mqtt_port_{0U};
  uint16_t ws_port_{0U};
  ClientHandlerCallback callback_;

  std::chrono::milliseconds worker_stop_timeout_;
  std::size_t worker_min_threads_{2U};
  std::size_t worker_max_threads_{0U};

  mutable std::mutex lifecycle_mutex_;
  std::atomic<bool> running_{false};
  StructuredTracer *structured_tracer_{nullptr};

  std::unique_ptr<IoReactor> io_reactor_;
  std::unique_ptr<WorkerPool> worker_pool_;
  ConnectionTable connection_table_;
  Broker *broker_{nullptr};
  const BrokerConfig *broker_config_{nullptr};

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;
};

} // namespace mqtt
