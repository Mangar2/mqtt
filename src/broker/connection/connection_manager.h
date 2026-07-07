#pragma once

/**
 * @file connection_manager.h
 * @brief Listener/reactor/worker orchestration for all client connections.
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <mutex>
#include <thread>
#include <vector>

#include "executor/worker_pool.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_listener.h"

namespace mqtt {

/**
 * @brief Forward declaration of Broker.
 */
class Broker;
/**
 * @brief Forward declaration of BrokerConfig.
 */
struct BrokerConfig;
/**
 * @brief Forward declaration of StructuredTracer.
 */
class StructuredTracer;

/**
 * @brief Manages listener sockets, reactor lifecycle, and worker job dispatch.
 */
class ConnectionManager {
public:
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port, Broker &broker,
                    const BrokerConfig &config,
                    std::size_t worker_min_threads = 2U,
                    std::size_t worker_max_threads = 0U);

  /**
   * @brief Destroy connection manager and release runtime resources.
   */
  ~ConnectionManager();

  ConnectionManager(const ConnectionManager &) = delete;
  ConnectionManager &operator=(const ConnectionManager &) = delete;

  /**
   * @brief Start listeners, reactor, and worker infrastructure.
   */
  void start();
  /**
   * @brief Stop listeners, reactor, and worker infrastructure.
   */
  void stop() noexcept;
  /**
   * @brief Query running state.
   * @return True when manager is running.
   */
  [[nodiscard]] bool is_running() const noexcept;

private:
  /**
   * @brief Accept one incoming connection from listener event.
   * @param listener_socket_handle Listener socket that became ready.
   * @param is_ws True when listener is WebSocket listener.
   */
  void handle_accept_ready(SocketHandle listener_socket_handle, bool is_ws);
  /**
   * @brief Process one scheduled connection job.
   * @param job Job payload to execute.
   */
  void handle_connection_job(const ConnectionJob &job);
  /**
   * @brief Watch keepalive deadlines and schedule closes.
   */
  void run_keepalive_watchdog();

  uint16_t mqtt_port_{0U};
  uint16_t ws_port_{0U};

  std::size_t worker_min_threads_{2U};
  std::size_t worker_max_threads_{0U};

  std::atomic<bool> running_{false};

  std::unique_ptr<IoReactor> io_reactor_;
  std::unique_ptr<WorkerPool> worker_pool_;
  ConnectionTable connection_table_;
  Broker &broker_;
  const BrokerConfig &broker_config_;

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;
  std::thread keepalive_watchdog_thread_;
  std::condition_variable keepalive_watchdog_cv_;
  std::mutex keepalive_watchdog_mutex_;
};

} // namespace mqtt
