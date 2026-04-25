#pragma once

/**
 * @file io_reactor.h
 * @brief Platform-neutral event reactor for non-blocking accept/read/write.
 */

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "monitoring/structured_tracer.h"

namespace mqtt {

/**
 * @brief Single-reactor event loop wrapper (kqueue on macOS/BSD, epoll on Linux, WSAPoll on Windows).
 *
 * The reactor owns one event-loop thread. Registration changes are synchronized
 * via an internal mutex. Callbacks are invoked on the reactor thread.
 */
class IoReactor {
public:
  /**
   * @brief Callback type for accept events.
   */
  using AcceptCallback = std::function<void(int)>;
  /**
   * @brief Callback type for read events.
   */
  using ReadCallback = std::function<void(int)>;
  /**
   * @brief Callback type for write events.
   */
  using WriteCallback = std::function<void(int)>;

  /**
   * @brief Construct stopped reactor instance.
   * @param tracer Optional structured tracer for diagnostics.
   */
  explicit IoReactor(StructuredTracer *tracer = nullptr);
  /**
   * @brief Destroy reactor and stop event thread.
   */
  ~IoReactor();

  IoReactor(const IoReactor &) = delete;
  IoReactor &operator=(const IoReactor &) = delete;

  /**
   * @brief Start reactor event loop.
   */
  void start();
  /**
   * @brief Stop reactor event loop.
   */
  void stop() noexcept;

  /**
   * @brief Register listener socket callback.
   * @param socket_fd Listener socket descriptor.
   * @param callback Callback for accept readiness.
   */
  void register_listener(int socket_fd, AcceptCallback callback);
  void register_connection(int socket_fd, ReadCallback read_callback,
                           WriteCallback write_callback);
  /**
   * @brief Arm write notifications for socket.
   * @param socket_fd Connection socket descriptor.
   */
  void arm_write(int socket_fd);
  /**
   * @brief Disarm write notifications for socket.
   * @param socket_fd Connection socket descriptor.
   */
  void disarm_write(int socket_fd);
  /**
   * @brief Unregister socket from reactor.
   * @param socket_fd Socket descriptor to remove.
   */
  void unregister(int socket_fd);
  /**
   * @brief Wake reactor event loop from another thread.
   */
  void wake() noexcept;

private:
  /**
   * @brief Callback bundle for one registered descriptor.
   */
  struct CallbackEntry {
    bool is_listener{false};
    bool write_armed{false};
    bool read_disarmed{false};
    AcceptCallback accept_callback;
    ReadCallback read_callback;
    WriteCallback write_callback;
  };

  /**
   * @brief Internal blocking event loop body.
   */
  void run_loop() noexcept;

  [[maybe_unused]] StructuredTracer *tracer_{nullptr};
  std::atomic<bool> running_{false};
  int backend_fd_{-1};
  int wake_read_fd_{-1};
  int wake_write_fd_{-1};
  std::thread event_thread_;
  std::mutex mutation_mutex_;
  std::unordered_map<int, CallbackEntry> callbacks_;
};

} // namespace mqtt

