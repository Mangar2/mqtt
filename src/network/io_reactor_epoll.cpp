#if defined(__linux__)

#include "network/io_reactor.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace mqtt {

namespace {

void close_backend_fd(int backend_fd) noexcept {
  if (backend_fd >= 0) {
    ::close(backend_fd);
  }
}

} // namespace

IoReactor::IoReactor() = default;

IoReactor::~IoReactor() { stop(); }

void IoReactor::start() {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (running_.load()) {
    return;
  }

  backend_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (backend_fd_ < 0) {
    throw std::runtime_error("IoReactor failed to create epoll backend");
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

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = socket_fd;
  (void)::epoll_ctl(backend_fd_, EPOLL_CTL_ADD, socket_fd, &event);
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

  epoll_event event{};
  event.events = EPOLLIN | EPOLLRDHUP;
  event.data.fd = socket_fd;
  (void)::epoll_ctl(backend_fd_, EPOLL_CTL_ADD, socket_fd, &event);
}

void IoReactor::arm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || backend_fd_ < 0 || iter == callbacks_.end() ||
      iter->second.is_listener || iter->second.write_armed) {
    return;
  }

  epoll_event event{};
  event.events = EPOLLIN | EPOLLRDHUP | EPOLLOUT;
  event.data.fd = socket_fd;
  (void)::epoll_ctl(backend_fd_, EPOLL_CTL_MOD, socket_fd, &event);
  iter->second.write_armed = true;
}

void IoReactor::disarm_write(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  auto iter = callbacks_.find(socket_fd);
  if (!running_.load() || backend_fd_ < 0 || iter == callbacks_.end() ||
      iter->second.is_listener || !iter->second.write_armed) {
    return;
  }

  epoll_event event{};
  event.events = EPOLLIN | EPOLLRDHUP;
  event.data.fd = socket_fd;
  (void)::epoll_ctl(backend_fd_, EPOLL_CTL_MOD, socket_fd, &event);
  iter->second.write_armed = false;
}

void IoReactor::unregister(int socket_fd) {
  std::lock_guard<std::mutex> lock_guard(mutation_mutex_);
  if (backend_fd_ >= 0) {
    (void)::epoll_ctl(backend_fd_, EPOLL_CTL_DEL, socket_fd, nullptr);
  }
  callbacks_.erase(socket_fd);
}

void IoReactor::run_loop() noexcept {
  std::array<epoll_event, 64> events{};

  while (running_.load()) {
    const int event_count =
        ::epoll_wait(backend_fd_, events.data(), static_cast<int>(events.size()),
                     100);
    if (event_count <= 0) {
      continue;
    }

    for (int event_index = 0; event_index < event_count; ++event_index) {
      const int socket_fd = events[event_index].data.fd;

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

      const auto event_flags = events[event_index].events;
      if (is_listener) {
        if ((event_flags & EPOLLIN) != 0U && accept_callback) {
          accept_callback(socket_fd);
        }
        continue;
      }

      if (((event_flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0U) &&
          read_callback) {
        read_callback(socket_fd);
      }
      if ((event_flags & EPOLLOUT) != 0U && write_callback) {
        write_callback(socket_fd);
      }
    }
  }
}

} // namespace mqtt

#endif

