/**
 * @file connection_manager.cpp
 * @brief ConnectionManager implementation (Module 23).
 */

#include "connection/connection_manager.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <system_error>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "network/socket_ops.h"
#include "monitoring/structured_tracer.h"

namespace mqtt {

namespace {

void close_socket_handle(SocketHandle socket_handle) noexcept {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

bool set_socket_blocking(SocketHandle socket_handle) noexcept {
#ifdef _WIN32
  u_long mode = 0;
  return ::ioctlsocket(static_cast<SOCKET>(socket_handle), FIONBIO, &mode) == 0;
#else
  const int current_flags = ::fcntl(static_cast<int>(socket_handle), F_GETFL, 0);
  if (current_flags < 0) {
    return false;
  }
  return ::fcntl(static_cast<int>(socket_handle), F_SETFL,
                 current_flags & ~O_NONBLOCK) == 0;
#endif
}

} // namespace

namespace {

[[nodiscard]] std::string io_result_to_string(IoResult io_result) {
  switch (io_result) {
  case IoResult::Ok:
    return "ok";
  case IoResult::WouldBlock:
    return "would_block";
  case IoResult::Closed:
    return "closed";
  case IoResult::Error:
    return "error";
  }
  return "unknown";
}

void emit_connection_event(StructuredTracer *structured_tracer,
                           TraceLevel trace_level, std::string_view event_info,
                           std::string_view listener_kind, int socket_fd,
                           std::string detail) {
  TRACE_GUARD(structured_tracer, trace_level, "connection") {
    TraceEvent event;
    event.level = trace_level;
    event.module = "connection";
    event.info = std::string(event_info);
    event.data.emplace_back("listener_kind", std::string(listener_kind));
    event.data.emplace_back("socket_fd", std::to_string(socket_fd));
    event.data.emplace_back("detail", std::move(detail));
    structured_tracer->emit(event);
  }
}

} // namespace

ConnectionManager::ConnectionManager(
    uint16_t mqtt_port, uint16_t ws_port, ClientHandlerCallback callback,
    std::chrono::milliseconds worker_stop_timeout,
    std::size_t worker_min_threads, std::size_t worker_max_threads,
    StructuredTracer *structured_tracer)
    : mqtt_port_(mqtt_port), ws_port_(ws_port), callback_(std::move(callback)),
      worker_stop_timeout_(worker_stop_timeout),
      worker_min_threads_(worker_min_threads),
      worker_max_threads_(worker_max_threads),
      structured_tracer_(structured_tracer) {}

    ConnectionManager::ConnectionManager(
      uint16_t mqtt_port, uint16_t ws_port, ClientHandlerCallback callback,
      std::chrono::milliseconds worker_stop_timeout,
      StructuredTracer *structured_tracer)
      : ConnectionManager(mqtt_port, ws_port, std::move(callback),
                worker_stop_timeout, 2U, 0U, structured_tracer) {}

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
    worker_pool_ = std::make_unique<WorkerPool>(
        [this](const ConnectionJob &job) { handle_connection_job(job); },
        worker_max_threads_);
    worker_pool_->start(worker_min_threads_);

    io_reactor_ = std::make_unique<IoReactor>();
    io_reactor_->start();

    if (mqtt_port_ != 0U) {
      mqtt_listener_ = TcpListener::listen(mqtt_port_);
      if (set_nonblocking(mqtt_listener_->fd()) != IoResult::Ok) {
        throw std::runtime_error(
            "ConnectionManager failed to set MQTT listener non-blocking");
      }
      io_reactor_->register_listener(
          static_cast<int>(mqtt_listener_->fd()),
          [this](int listener_socket_fd) {
            handle_accept_ready(static_cast<SocketHandle>(listener_socket_fd),
                                false);
          });
    }

    if (ws_port_ != 0U) {
      ws_listener_ = TcpListener::listen(ws_port_);
      if (set_nonblocking(ws_listener_->fd()) != IoResult::Ok) {
        throw std::runtime_error(
            "ConnectionManager failed to set WebSocket listener non-blocking");
      }
      io_reactor_->register_listener(
          static_cast<int>(ws_listener_->fd()),
          [this](int listener_socket_fd) {
            handle_accept_ready(static_cast<SocketHandle>(listener_socket_fd),
                                true);
          });
    }
  } catch (...) {
    running_.store(false);

    if (worker_pool_) {
      worker_pool_->stop();
      worker_pool_.reset();
    }

    if (io_reactor_) {
      io_reactor_->stop();
      io_reactor_.reset();
    }

    if (mqtt_listener_.has_value()) {
      mqtt_listener_->close();
      mqtt_listener_.reset();
    }
    if (ws_listener_.has_value()) {
      ws_listener_->close();
      ws_listener_.reset();
    }
    throw;
  }
}

void ConnectionManager::stop() noexcept {
  std::lock_guard<std::mutex> lock_guard(lifecycle_mutex_);
  if (!running_.load()) {
    return;
  }

  running_.store(false);

  if (io_reactor_) {
    io_reactor_->stop();
    io_reactor_.reset();
  }

  if (mqtt_listener_.has_value()) {
    mqtt_listener_->close();
    mqtt_listener_.reset();
  }
  if (ws_listener_.has_value()) {
    ws_listener_->close();
    ws_listener_.reset();
  }

  const auto deadline = std::chrono::steady_clock::now() + worker_stop_timeout_;
  const std::vector<SocketHandle> socket_handles =
      connection_table_.snapshot_socket_handles();
  for (SocketHandle socket_handle : socket_handles) {
    TcpConnection::shutdown_socket(socket_handle);
  }

  while (std::chrono::steady_clock::now() < deadline) {
    if (connection_table_.size() == 0U) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (worker_pool_) {
    worker_pool_->stop();
    worker_pool_.reset();
  }
}

bool ConnectionManager::is_running() const noexcept { return running_.load(); }

std::string ConnectionManager::io_result_to_string_for_test(IoResult io_result) {
  return io_result_to_string(io_result);
}

bool ConnectionManager::set_socket_blocking_for_test(
    SocketHandle socket_handle) noexcept {
  return set_socket_blocking(socket_handle);
}

void ConnectionManager::close_socket_handle_for_test(
    SocketHandle socket_handle) noexcept {
  close_socket_handle(socket_handle);
}

void ConnectionManager::handle_accept_ready(SocketHandle listener_socket_handle,
                                            bool is_ws) {
  const std::string_view listener_kind = is_ws ? "ws" : "mqtt";
  while (running_.load()) {
    SocketHandle accepted_socket_handle = k_invalid_socket;
    const IoResult accept_result =
        nb_accept(listener_socket_handle, &accepted_socket_handle);

    if (accept_result == IoResult::WouldBlock) {
      break;
    }
    if (accept_result != IoResult::Ok ||
        accepted_socket_handle == k_invalid_socket) {
      emit_connection_event(structured_tracer_, TraceLevel::Trace,
                            "listener_accept_not_ok", listener_kind,
                            static_cast<int>(listener_socket_handle),
                            "accept_result=" + io_result_to_string(accept_result));
      break;
    }

    const int socket_fd = static_cast<int>(accepted_socket_handle);
    if (!connection_table_.add(ConnectionSlot(accepted_socket_handle))) {
      emit_connection_event(
          structured_tracer_, TraceLevel::Trace, "connection_slot_add_rejected",
          listener_kind, socket_fd, "connection_slot_already_exists");
      close_socket_handle(accepted_socket_handle);
      continue;
    }

    if (!worker_pool_) {
      [[maybe_unused]] const bool removed = connection_table_.remove(socket_fd);
      close_socket_handle(accepted_socket_handle);
      continue;
    }

    try {
      worker_pool_->submit(ConnectionJob{
          .type = JobType::Accept,
          .connection_fd = socket_fd,
          .payload = AcceptJobPayload{.socket_handle = accepted_socket_handle,
                                      .websocket_connection = is_ws}});
    } catch (const std::exception &exception) {
      emit_connection_event(
          structured_tracer_, TraceLevel::Warning, "client_thread_start_failed",
          listener_kind, socket_fd,
          "worker_submit_error=" + std::string(exception.what()));
      [[maybe_unused]] const bool removed = connection_table_.remove(socket_fd);
      close_socket_handle(accepted_socket_handle);
      continue;
    }
  }
}

void ConnectionManager::handle_connection_job(const ConnectionJob &job) {
  if (job.type != JobType::Accept) {
    return;
  }

  const AcceptJobPayload *accept_payload =
      std::get_if<AcceptJobPayload>(&job.payload);
  if (accept_payload == nullptr ||
      accept_payload->socket_handle == k_invalid_socket) {
    [[maybe_unused]] const bool removed =
        connection_table_.remove(job.connection_fd);
    return;
  }

  auto connection =
      std::make_unique<TcpConnection>(accept_payload->socket_handle);
  try {
    callback_(std::move(connection), accept_payload->websocket_connection);
  } catch (...) {
    // Individual client failures must not terminate worker dispatch.
  }

  [[maybe_unused]] const bool removed = connection_table_.remove(job.connection_fd);
}

} // namespace mqtt
