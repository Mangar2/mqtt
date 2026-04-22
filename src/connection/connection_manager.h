#pragma once

/**
 * @file connection_manager.h
 * @brief Listener/reactor/worker orchestration for all client connections.
 */

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "executor/worker_pool.h"
#include "network/connection_table.h"
#include "network/io_reactor.h"
#include "network/socket_ops.h"
#include "network/tcp_listener.h"

namespace mqtt {

class Broker;
struct BrokerConfig;
class StructuredTracer;

/**
 * @brief Manages listener sockets, reactor lifecycle, and worker job dispatch.
 */
class ConnectionManager {
public:
  ConnectionManager(uint16_t mqtt_port, uint16_t ws_port, Broker &broker,
                    const BrokerConfig &config,
                    std::size_t worker_min_threads = 2U,
                    std::size_t worker_max_threads = 0U,
                    StructuredTracer *structured_tracer = nullptr);

  ~ConnectionManager();

  ConnectionManager(const ConnectionManager &) = delete;
  ConnectionManager &operator=(const ConnectionManager &) = delete;

  void start();
  void stop() noexcept;
  [[nodiscard]] bool is_running() const noexcept;

private:
  void handle_accept_ready(SocketHandle listener_socket_handle, bool is_ws);
  void handle_connection_job(const ConnectionJob &job);

  uint16_t mqtt_port_{0U};
  uint16_t ws_port_{0U};

  std::size_t worker_min_threads_{2U};
  std::size_t worker_max_threads_{0U};

  std::atomic<bool> running_{false};
  StructuredTracer *structured_tracer_{nullptr};

  std::unique_ptr<IoReactor> io_reactor_;
  std::unique_ptr<WorkerPool> worker_pool_;
  ConnectionTable connection_table_;
  Broker &broker_;
  const BrokerConfig &broker_config_;

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;
};

} // namespace mqtt
