/**
 * @file connection_manager.cpp
 * @brief ConnectionManager implementation (Module 23).
 */

#include "connection/connection_manager.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mqtt {

ConnectionManager::ConnectionManager(
    uint16_t mqtt_port, uint16_t ws_port, ClientHandlerCallback callback,
    std::chrono::milliseconds client_join_timeout)
    : mqtt_port_(mqtt_port), ws_port_(ws_port), callback_(std::move(callback)),
      client_join_timeout_(client_join_timeout) {}

ConnectionManager::~ConnectionManager() { stop(); }

void ConnectionManager::start() {
  std::lock_guard<std::mutex> lock_guard(lifecycle_mutex_);
  if (running_.load()) {
    return;
  }

  if (mqtt_port_ != 0U && ws_port_ != 0U && mqtt_port_ == ws_port_) {
    throw std::runtime_error(
        "ConnectionManager requires distinct MQTT and WebSocket ports");
  }

  running_.store(true);

  try {
    if (mqtt_port_ != 0U) {
      mqtt_listener_ = TcpListener::listen(mqtt_port_);
      spawn_accept_loop(*mqtt_listener_, false);
    }
    if (ws_port_ != 0U) {
      ws_listener_ = TcpListener::listen(ws_port_);
      spawn_accept_loop(*ws_listener_, true);
    }
  } catch (...) {
    running_.store(false);
    if (mqtt_listener_.has_value()) {
      mqtt_listener_->close();
      mqtt_listener_.reset();
    }
    if (ws_listener_.has_value()) {
      ws_listener_->close();
      ws_listener_.reset();
    }
    join_accept_threads();
    throw;
  }
}

void ConnectionManager::stop() noexcept {
  std::lock_guard<std::mutex> lock_guard(lifecycle_mutex_);
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  if (mqtt_listener_.has_value()) {
    mqtt_listener_->close();
    mqtt_listener_.reset();
  }
  if (ws_listener_.has_value()) {
    ws_listener_->close();
    ws_listener_.reset();
  }

  join_accept_threads();

  const auto deadline = std::chrono::steady_clock::now() + client_join_timeout_;
  while (std::chrono::steady_clock::now() < deadline) {
    cleanup_finished();

    {
      std::lock_guard<std::mutex> client_lock_guard(client_threads_mutex_);
      if (client_threads_.empty()) {
        break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // After timeout, request socket shutdown to unblock blocked reads.
  {
    std::lock_guard<std::mutex> client_lock_guard(client_threads_mutex_);
    for (const ClientThreadEntry &entry : client_threads_) {
      TcpConnection::shutdown_socket(entry.socket_handle);
    }
  }

  join_all_clients();
}

bool ConnectionManager::is_running() const noexcept { return running_.load(); }

void ConnectionManager::spawn_accept_loop(TcpListener &listener, bool is_ws) {
  accept_threads_.emplace_back(
      [this, &listener, is_ws]() { accept_loop(listener, is_ws); });
}

void ConnectionManager::accept_loop(TcpListener &listener, bool is_ws) {
  while (running_.load()) {
    try {
      std::unique_ptr<TcpConnection> connection = listener.accept();
      if (!connection) {
        continue;
      }

      cleanup_finished();

      auto finished = std::make_shared<std::atomic<bool>>(false);
      SocketHandle socket_handle = connection->fd();

      std::thread client_thread([this, finished, is_ws,
                                 connection = std::move(connection)]() mutable {
        try {
          callback_(std::move(connection), is_ws);
        } catch (...) {
          // Individual client failures must not terminate the accept loop.
        }
        finished->store(true);
      });

      std::lock_guard<std::mutex> client_lock_guard(client_threads_mutex_);
      client_threads_.push_back(
          ClientThreadEntry{.thread = std::move(client_thread),
                            .finished = std::move(finished),
                            .socket_handle = socket_handle});
    } catch (...) {
      break;
    }
  }
}

void ConnectionManager::cleanup_finished() {
  std::lock_guard<std::mutex> client_lock_guard(client_threads_mutex_);

  auto erase_begin = std::remove_if(client_threads_.begin(), client_threads_.end(),
                                    [](ClientThreadEntry &entry) {
                                      if (!entry.finished->load()) {
                                        return false;
                                      }
                                      if (entry.thread.joinable()) {
                                        entry.thread.join();
                                      }
                                      return true;
                                    });
  client_threads_.erase(erase_begin, client_threads_.end());
}

void ConnectionManager::join_accept_threads() noexcept {
  for (std::thread &thread : accept_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  accept_threads_.clear();
}

void ConnectionManager::join_all_clients() noexcept {
  std::lock_guard<std::mutex> client_lock_guard(client_threads_mutex_);
  for (ClientThreadEntry &entry : client_threads_) {
    if (entry.thread.joinable()) {
      entry.thread.join();
    }
  }
  client_threads_.clear();
}

} // namespace mqtt
