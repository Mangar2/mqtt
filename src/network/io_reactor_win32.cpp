#if defined(_WIN32)

#include "network/io_reactor.h"

#include <winsock2.h>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace mqtt {

namespace {

[[nodiscard]] SOCKET to_socket(int socket_fd) noexcept {
  return static_cast<SOCKET>(socket_fd);
}

[[nodiscard]] short make_poll_events(bool is_listener, bool write_armed) noexcept {
  short events = POLLRDNORM;
  if (!is_listener && write_armed) {
    events = static_cast<short>(events | POLLWRNORM);
  }
  return events;
}

[[nodiscard]] bool has_read_event(short revents) noexcept {
  return (revents & (POLLRDNORM | POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0;
}

[[nodiscard]] bool has_write_event(short revents) noexcept {
  return (revents & (POLLWRNORM | POLLOUT)) != 0;
}

struct PollEntry {
  int socket_fd;
  bool is_listener;
  IoReactor::AcceptCallback accept_callback;
  IoReactor::ReadCallback read_callback;
  IoReactor::WriteCallback write_callback;
};

} // namespace

IoReactor::IoReactor() = default;

IoReactor::~IoReactor() { stop(); }

void IoReactor::start() {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (running_.load()) {
    return;
  }

  running_.store(true);
  event_thread_ = std::thread([this]() { run_loop(); });
}

void IoReactor::stop() noexcept {
  {
    std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
    if (!running_.load()) {
      return;
    }
    running_.store(false);
  }

  if (event_thread_.joinable()) {
    event_thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
    callbacks_.clear();
  }
}

void IoReactor::wake() noexcept {
  // WSAPoll has no dedicated user wake fd; stop/registration changes are
  // observed on the next poll timeout.
}

void IoReactor::register_listener(int socket_fd, AcceptCallback callback) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (!running_.load()) {
    throw std::runtime_error("IoReactor must be started before registration");
  }

  CallbackEntry entry{};
  entry.is_listener = true;
  entry.accept_callback = std::move(callback);
  callbacks_[socket_fd] = std::move(entry);
}

void IoReactor::register_connection(int socket_fd, ReadCallback read_callback,
                                    WriteCallback write_callback) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (!running_.load()) {
    throw std::runtime_error("IoReactor must be started before registration");
  }

  CallbackEntry entry{};
  entry.is_listener = false;
  entry.read_callback = std::move(read_callback);
  entry.write_callback = std::move(write_callback);
  callbacks_[socket_fd] = std::move(entry);
}

void IoReactor::arm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || iter == callbacks_.end() || iter->second.is_listener) {
    return;
  }
  iter->second.write_armed = true;
}

void IoReactor::disarm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || iter == callbacks_.end() || iter->second.is_listener) {
    return;
  }
  iter->second.write_armed = false;
}

void IoReactor::unregister(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  callbacks_.erase(socket_fd);
}

void IoReactor::run_loop() noexcept {
  using namespace std::chrono_literals;

  while (running_.load()) {
    std::vector<WSAPOLLFD> poll_fds;
    std::vector<PollEntry> poll_entries;
    {
      std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
      poll_fds.reserve(callbacks_.size());
      poll_entries.reserve(callbacks_.size());
      for (const auto &[socket_fd, entry] : callbacks_) {
        WSAPOLLFD poll_fd{};
        poll_fd.fd = to_socket(socket_fd);
        poll_fd.events = make_poll_events(entry.is_listener, entry.write_armed);
        poll_fd.revents = 0;
        poll_fds.push_back(poll_fd);

        poll_entries.push_back(PollEntry{.socket_fd = socket_fd,
                                         .is_listener = entry.is_listener,
                                         .accept_callback = entry.accept_callback,
                                         .read_callback = entry.read_callback,
                                         .write_callback = entry.write_callback});
      }
    }

    if (poll_fds.empty()) {
      std::this_thread::sleep_for(25ms);
      continue;
    }

    const int ready_count = ::WSAPoll(poll_fds.data(), static_cast<ULONG>(poll_fds.size()),
                                      100);
    if (ready_count <= 0) {
      continue;
    }

    for (std::size_t index = 0; index < poll_fds.size(); ++index) {
      const short revents = poll_fds[index].revents;
      if (revents == 0) {
        continue;
      }

      const PollEntry &entry = poll_entries[index];
      if (entry.is_listener) {
        if (has_read_event(revents) && entry.accept_callback) {
          entry.accept_callback(entry.socket_fd);
        }
        continue;
      }

      if (has_read_event(revents) && entry.read_callback) {
        entry.read_callback(entry.socket_fd);
      }
      if (has_write_event(revents) && entry.write_callback) {
        entry.write_callback(entry.socket_fd);
      }
    }
  }
}

} // namespace mqtt

#endif
