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
#include <thread>
#include <vector>

#include "network/tcp_connection.h"
#include "network/tcp_listener.h"

namespace mqtt {

/**
 * @brief Manages listener sockets, accept loops, and tracked client threads.
 *
 * This class owns the networking thread lifecycle that was previously embedded
 * in `Broker`. It starts one accept loop per enabled listener and spawns one
 * tracked thread per accepted client connection.
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
                        std::chrono::seconds(2));

  /** @brief Destructor that performs best-effort shutdown via `stop()`. */
  ~ConnectionManager();

  ConnectionManager(const ConnectionManager &) = delete;
  ConnectionManager &operator=(const ConnectionManager &) = delete;

  /**
   * @brief Start all configured listeners and accept loops.
   *
   * Throws on socket/listener failures and leaves the manager stopped.
   */
  void start();

  /**
   * @brief Stop listeners, join accept loops, and terminate client threads.
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
  /**
   * @brief Tracked client thread metadata.
   */
  struct ClientThreadEntry {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> finished;
    SocketHandle socket_handle{0U};
  };

  /**
   * @brief Spawn one accept-loop thread for a listener.
   * @param listener Listener instance.
   * @param is_ws `true` for WebSocket listener, `false` for MQTT listener.
   */
  void spawn_accept_loop(TcpListener &listener, bool is_ws);

  /**
   * @brief Accept loop body for one listener.
   * @param listener Listener used for blocking accept.
   * @param is_ws `true` for WebSocket listener, `false` for MQTT listener.
   */
  void accept_loop(TcpListener &listener, bool is_ws);

  /** @brief Join and remove finished client threads from the registry. */
  void cleanup_finished();

  /** @brief Best-effort join of all accept-loop threads. */
  void join_accept_threads() noexcept;

  /** @brief Best-effort join of all client threads. */
  void join_all_clients() noexcept;

  uint16_t mqtt_port_{0U};
  uint16_t ws_port_{0U};
  ClientHandlerCallback callback_;

  std::chrono::milliseconds client_join_timeout_;

  mutable std::mutex lifecycle_mutex_;
  std::atomic<bool> running_{false};

  std::optional<TcpListener> mqtt_listener_;
  std::optional<TcpListener> ws_listener_;
  std::vector<std::thread> accept_threads_;

  std::mutex client_threads_mutex_;
  std::vector<ClientThreadEntry> client_threads_;
};

} // namespace mqtt
