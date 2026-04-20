#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)

#include "network/io_reactor.h"

#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mqtt {

namespace {

void close_backend_fd(int backend_fd) noexcept {
  if (backend_fd >= 0) {
    ::close(backend_fd);
  }
}

std::vector<struct kevent>
make_delete_changes(const int socket_fd) {
  std::vector<struct kevent> changes;
  changes.reserve(3U);
  struct kevent delete_read{};
  EV_SET(&delete_read, static_cast<uintptr_t>(socket_fd), EVFILT_READ, EV_DELETE,
         0, 0, nullptr);
  changes.push_back(delete_read);

  struct kevent delete_write{};
  EV_SET(&delete_write, static_cast<uintptr_t>(socket_fd), EVFILT_WRITE,
         EV_DELETE, 0, 0, nullptr);
  changes.push_back(delete_write);

#ifdef EVFILT_EXCEPT
  struct kevent delete_except{};
  EV_SET(&delete_except, static_cast<uintptr_t>(socket_fd), EVFILT_EXCEPT,
         EV_DELETE, 0, 0, nullptr);
  changes.push_back(delete_except);
#endif
  return changes;
}

} // namespace

IoReactor::IoReactor() = default;

IoReactor::~IoReactor() { stop(); }

void IoReactor::start() {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (running_.load()) {
    return;
  }

  backend_fd_ = ::kqueue();
  if (backend_fd_ < 0) {
    throw std::runtime_error("IoReactor failed to create kqueue backend");
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
    close_backend_fd(backend_fd_);
    backend_fd_ = -1;
  }
}

void IoReactor::register_listener(int socket_fd, AcceptCallback callback) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (!running_.load() || backend_fd_ < 0) {
    throw std::runtime_error("IoReactor must be started before registration");
  }

  CallbackEntry entry{};
  entry.is_listener = true;
  entry.accept_callback = std::move(callback);
  callbacks_[socket_fd] = std::move(entry);

  std::array<struct kevent, 2> changes{};
  EV_SET(&changes[0], static_cast<uintptr_t>(socket_fd), EVFILT_READ,
         EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
#ifdef EVFILT_EXCEPT
  EV_SET(&changes[1], static_cast<uintptr_t>(socket_fd), EVFILT_EXCEPT,
         EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
  constexpr int k_change_count = 2;
#else
  constexpr int k_change_count = 1;
#endif

  (void)::kevent(backend_fd_, changes.data(), k_change_count, nullptr, 0,
                 nullptr);
}

void IoReactor::register_connection(int socket_fd, ReadCallback read_callback,
                                    WriteCallback write_callback) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (!running_.load() || backend_fd_ < 0) {
    throw std::runtime_error("IoReactor must be started before registration");
  }

  CallbackEntry entry{};
  entry.is_listener = false;
  entry.read_callback = std::move(read_callback);
  entry.write_callback = std::move(write_callback);
  callbacks_[socket_fd] = std::move(entry);

  std::array<struct kevent, 2> changes{};
  EV_SET(&changes[0], static_cast<uintptr_t>(socket_fd), EVFILT_READ,
         EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
  EV_SET(&changes[1], static_cast<uintptr_t>(socket_fd), EVFILT_WRITE,
         EV_ADD | EV_DISABLE | EV_CLEAR, 0, 0, nullptr);
  (void)::kevent(backend_fd_, changes.data(), static_cast<int>(changes.size()),
                 nullptr, 0, nullptr);
}

void IoReactor::arm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || backend_fd_ < 0 || iter == callbacks_.end() ||
      iter->second.is_listener || iter->second.write_armed) {
    return;
  }

  struct kevent change{};
  EV_SET(&change, static_cast<uintptr_t>(socket_fd), EVFILT_WRITE, EV_ENABLE, 0,
         0, nullptr);
  (void)::kevent(backend_fd_, &change, 1, nullptr, 0, nullptr);
  iter->second.write_armed = true;
}

void IoReactor::disarm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || backend_fd_ < 0 || iter == callbacks_.end() ||
      iter->second.is_listener || !iter->second.write_armed) {
    return;
  }

  struct kevent change{};
  EV_SET(&change, static_cast<uintptr_t>(socket_fd), EVFILT_WRITE, EV_DISABLE, 0,
         0, nullptr);
  (void)::kevent(backend_fd_, &change, 1, nullptr, 0, nullptr);
  iter->second.write_armed = false;
}

void IoReactor::unregister(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (backend_fd_ >= 0) {
    std::vector<struct kevent> changes = make_delete_changes(socket_fd);
    (void)::kevent(backend_fd_, changes.data(), static_cast<int>(changes.size()),
                   nullptr, 0, nullptr);
  }
  callbacks_.erase(socket_fd);
}

void IoReactor::run_loop() noexcept {
  std::array<struct kevent, 64> events{};
  const struct timespec timeout{.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000};

  while (running_.load()) {
    const int event_count =
        ::kevent(backend_fd_, nullptr, 0, events.data(),
                 static_cast<int>(events.size()), &timeout);
    if (event_count <= 0) {
      continue;
    }

    for (int event_index = 0; event_index < event_count; ++event_index) {
      const int socket_fd = static_cast<int>(events[event_index].ident);

      AcceptCallback accept_callback;
      ReadCallback read_callback;
      WriteCallback write_callback;
      bool is_listener = false;

      {
        std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
        const auto callback_iter = callbacks_.find(socket_fd);
        if (callback_iter == callbacks_.end()) {
          continue;
        }
        is_listener = callback_iter->second.is_listener;
        accept_callback = callback_iter->second.accept_callback;
        read_callback = callback_iter->second.read_callback;
        write_callback = callback_iter->second.write_callback;
      }

      if (events[event_index].filter == EVFILT_READ) {
        if (is_listener) {
          if (accept_callback) {
            accept_callback(socket_fd);
          }
        } else if (read_callback) {
          read_callback(socket_fd);
        }
        continue;
      }

#ifdef EVFILT_EXCEPT
      if (events[event_index].filter == EVFILT_EXCEPT) {
        if (is_listener) {
          if (accept_callback) {
            accept_callback(socket_fd);
          }
        } else if (read_callback) {
          read_callback(socket_fd);
        }
        continue;
      }
#endif

      if (events[event_index].filter == EVFILT_WRITE && write_callback) {
        write_callback(socket_fd);
      }
    }
  }
}

} // namespace mqtt

#endif

