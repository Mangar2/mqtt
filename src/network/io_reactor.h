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

namespace mqtt {

/**
 * @brief Single-reactor event loop wrapper (kqueue on macOS/BSD, epoll on Linux).
 *
 * The reactor owns one event-loop thread. Registration changes are synchronized
 * via an internal mutex. Callbacks are invoked on the reactor thread.
 */
class IoReactor {
public:
  using AcceptCallback = std::function<void(int)>;
  using ReadCallback = std::function<void(int)>;
  using WriteCallback = std::function<void(int)>;

  IoReactor();
  ~IoReactor();

  IoReactor(const IoReactor &) = delete;
  IoReactor &operator=(const IoReactor &) = delete;

  void start();
  void stop() noexcept;

  void register_listener(int socket_fd, AcceptCallback callback);
  void register_connection(int socket_fd, ReadCallback read_callback,
                           WriteCallback write_callback);
  void arm_write(int socket_fd);
  void disarm_write(int socket_fd);
  void unregister(int socket_fd);
  void wake() noexcept;

private:
  struct CallbackEntry {
    bool is_listener{false};
    bool write_armed{false};
    AcceptCallback accept_callback;
    ReadCallback read_callback;
    WriteCallback write_callback;
  };

  void run_loop() noexcept;

  std::atomic<bool> running_{false};
  int backend_fd_{-1};
  int wake_read_fd_{-1};
  int wake_write_fd_{-1};
  std::thread event_thread_;
  std::mutex mutation_mutex_;
  std::unordered_map<int, CallbackEntry> callbacks_;
};

} // namespace mqtt

