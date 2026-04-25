/**
 * @file connection_manager.cpp
 * @brief ConnectionManager implementation (Module 23).
 */

#include "connection/connection_manager.h"

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "connection/client_handler.h"
#include "connection/connection_flow_support.h"
#include "monitoring/structured_tracer.h"
#include "network/socket_ops.h"
#include "network/tcp_connection.h"
#include "transport/websocket_transport.h"

namespace mqtt {

namespace {

void send_server_shutting_down_disconnect(ConnectionSession &session) {
  WriteBuffer frame =
    encode_disconnect_packet(ReasonCode::ServerShuttingDown);
  if (session.is_websocket()) {
  const std::vector<uint8_t> ws_frame = WebSocketTransport::encode_frame(
    std::span<const uint8_t>(frame.data(), frame.size()));
  (void)session.connection().write(
    std::span<const uint8_t>(ws_frame.data(), ws_frame.size()));
  return;
  }

  (void)session.connection().write(
    std::span<const uint8_t>(frame.data(), frame.size()));
}

} // namespace

ConnectionManager::ConnectionManager(uint16_t mqtt_port, uint16_t ws_port,
                                     Broker &broker,
                                     const BrokerConfig &config,
                                     std::size_t worker_min_threads,
                                     std::size_t worker_max_threads)
    : mqtt_port_(mqtt_port), ws_port_(ws_port),
      worker_min_threads_(worker_min_threads),
      worker_max_threads_(worker_max_threads),
      broker_(broker),
      broker_config_(config) {}

ConnectionManager::~ConnectionManager() { stop(); }

void ConnectionManager::start() {
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
          worker_max_threads_, &broker_.structured_tracer());
    worker_pool_->start(worker_min_threads_);
    broker_.set_job_scheduler(&worker_pool_->job_scheduler());

    io_reactor_ = std::make_unique<IoReactor>(&broker_.structured_tracer());
    io_reactor_->start();

    if (mqtt_port_ != 0U) {
      mqtt_listener_ = TcpListener::listen(mqtt_port_);
      if (set_nonblocking(mqtt_listener_->fd()) != IoResult::Ok) {
        throw std::runtime_error(
            "ConnectionManager failed to set MQTT listener non-blocking");
      }
      io_reactor_->register_listener(
          static_cast<int>(mqtt_listener_->fd()), [this](int listener_fd) {
            handle_accept_ready(static_cast<SocketHandle>(listener_fd), false);
          });
    }

    if (ws_port_ != 0U) {
      ws_listener_ = TcpListener::listen(ws_port_);
      if (set_nonblocking(ws_listener_->fd()) != IoResult::Ok) {
        throw std::runtime_error(
            "ConnectionManager failed to set WebSocket listener non-blocking");
      }
      io_reactor_->register_listener(
          static_cast<int>(ws_listener_->fd()), [this](int listener_fd) {
            handle_accept_ready(static_cast<SocketHandle>(listener_fd), true);
          });
    }

    keepalive_watchdog_thread_ = std::thread([this]() {
      run_keepalive_watchdog();
    });
  } catch (...) {
    running_.store(false);

    if (keepalive_watchdog_thread_.joinable()) {
      keepalive_watchdog_thread_.join();
    }

    if (ws_listener_.has_value()) {
      ws_listener_->close();
      ws_listener_.reset();
    }
    if (mqtt_listener_.has_value()) {
      mqtt_listener_->close();
      mqtt_listener_.reset();
    }

    if (io_reactor_) {
      io_reactor_->stop();
      io_reactor_.reset();
    }

    if (worker_pool_) {
      worker_pool_->stop();
      worker_pool_.reset();
      broker_.set_job_scheduler(nullptr);
    }

    throw;
  }
}

void ConnectionManager::stop() noexcept {
  if (!running_.load()) {
    return;
  }

  running_.store(false);
  keepalive_watchdog_cv_.notify_all();

  if (keepalive_watchdog_thread_.joinable()) {
    keepalive_watchdog_thread_.join();
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

  if (worker_pool_) {
    worker_pool_->stop();
    worker_pool_.reset();
  }
  broker_.set_job_scheduler(nullptr);

  const std::vector<SocketHandle> sockets_snapshot =
      connection_table_.snapshot_socket_handles();
  for (SocketHandle socket_handle : sockets_snapshot) {
    const int connection_fd = static_cast<int>(socket_handle);
    ConnectionTable::Entry *entry = connection_table_.find(connection_fd);
    if (entry != nullptr && entry->session != nullptr) {
      send_server_shutting_down_disconnect(*entry->session);
    }
  }

  for (SocketHandle socket_handle : connection_table_.snapshot_socket_handles()) {
    TcpConnection::shutdown_socket(socket_handle);
  }

  connection_table_.clear();
}

bool ConnectionManager::is_running() const noexcept { return running_.load(); }

void ConnectionManager::run_keepalive_watchdog() {
  using namespace std::chrono_literals;

  constexpr auto k_idle_scan_interval = 100ms;
  constexpr auto k_due_epsilon = 1ms;

  while (running_.load()) {
    const auto now = std::chrono::steady_clock::now();
    auto next_deadline = std::chrono::steady_clock::time_point::max();
    std::vector<int> due_decode_fds;

    if (worker_pool_ == nullptr) {
      std::unique_lock<std::mutex> lock_guard(keepalive_watchdog_mutex_);
      keepalive_watchdog_cv_.wait_for(lock_guard, k_idle_scan_interval,
                                      [this] { return !running_.load(); });
      continue;
    }

    const std::vector<SocketHandle> socket_handles =
        connection_table_.snapshot_socket_handles();

    for (SocketHandle socket_handle : socket_handles) {
      const int connection_fd = static_cast<int>(socket_handle);
      if (ConnectionTable::Entry *entry = connection_table_.find(connection_fd);
          entry != nullptr) {
        entry->slot.maybe_trim_write_capacity(now);

        if (entry->session != nullptr) {
          const auto decode_deadline =
              client_handler::next_decode_deadline(*entry->session);
          if (decode_deadline.has_value()) {
            if (*decode_deadline <= now + k_due_epsilon) {
              due_decode_fds.push_back(connection_fd);
            } else if (*decode_deadline < next_deadline) {
              next_deadline = *decode_deadline;
            }
          }
        }
      }
    }

    for (int connection_fd : due_decode_fds) {
      try {
        worker_pool_->submit(ConnectionJob{.type = JobType::Decode,
                                           .connection_fd = connection_fd,
                                           .payload = DecodeJobPayload{}});
      } catch (const std::exception &) {
      }
    }

    if (!due_decode_fds.empty()) {
      TRACE_GUARD(&broker_.structured_tracer(), TraceLevel::Info, "connection") {
        std::string fd_list;
        for (int connection_fd : due_decode_fds) {
          if (!fd_list.empty()) { fd_list += ','; }
          fd_list += std::to_string(connection_fd);
        }
        TraceEvent event;
        event.level = TraceLevel::Info;
        event.module = "connection";
        event.info = "watchdog_due_decode";
        event.data.emplace_back("due_count", std::to_string(due_decode_fds.size()));
        event.data.emplace_back("fds", fd_list);
        broker_.structured_tracer().emit(event);
      }
    }

    TRACE_GUARD(&broker_.structured_tracer(), TraceLevel::Trace, "connection") {
      const auto ms_until_deadline =
          next_deadline == std::chrono::steady_clock::time_point::max()
              ? -1LL
              : static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        next_deadline - std::chrono::steady_clock::now())
                        .count());
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "connection";
      event.info = "watchdog_sleep";
      event.data.emplace_back("handle_count",
                              std::to_string(socket_handles.size()));
      event.data.emplace_back("due_count",
                              std::to_string(due_decode_fds.size()));
      event.data.emplace_back("ms_until_deadline",
                              std::to_string(ms_until_deadline));
      broker_.structured_tracer().emit(event);
    }

    std::unique_lock<std::mutex> lock_guard(keepalive_watchdog_mutex_);
    if (!running_.load()) {
      break;
    }

    if (next_deadline == std::chrono::steady_clock::time_point::max()) {
      keepalive_watchdog_cv_.wait_for(lock_guard, k_idle_scan_interval,
                                      [this] { return !running_.load(); });
    } else {
      const auto now_for_wait = std::chrono::steady_clock::now();
      const auto max_sleep_deadline = now_for_wait + k_idle_scan_interval;
      const auto wake_deadline =
          (next_deadline < max_sleep_deadline) ? next_deadline
                                               : max_sleep_deadline;
      keepalive_watchdog_cv_.wait_until(lock_guard, wake_deadline,
                                        [this] { return !running_.load(); });
    }
  }
}

void ConnectionManager::handle_accept_ready(SocketHandle listener_socket_handle,
                                            bool is_ws) {
  (void)is_ws;
  while (running_.load()) {
    SocketHandle accepted_socket_handle = k_invalid_socket;
    const IoResult accept_result =
      nb_accept(listener_socket_handle, &accepted_socket_handle,
            !is_ws);

    if (accept_result == IoResult::WouldBlock) {
      break;
    }
    if (accept_result != IoResult::Ok ||
        accepted_socket_handle == k_invalid_socket) {
      break;
    }

    if (!worker_pool_) {
      TcpConnection::shutdown_socket(accepted_socket_handle);
      continue;
    }

    const int socket_fd = static_cast<int>(accepted_socket_handle);
    try {
      worker_pool_->submit(ConnectionJob{
          .type = JobType::Accept,
          .connection_fd = socket_fd,
          .payload = AcceptJobPayload{.socket_handle = accepted_socket_handle,
                                      .websocket_connection = is_ws}});
    } catch (const std::exception &) {
      TcpConnection::shutdown_socket(accepted_socket_handle);
    }
  }
}

void ConnectionManager::handle_connection_job(const ConnectionJob &job) {
  if (io_reactor_ == nullptr || worker_pool_ == nullptr) {
    return;
  }

  try {
    switch (job.type) {
    case JobType::Accept: {
      const AcceptJobPayload *accept_payload =
          std::get_if<AcceptJobPayload>(&job.payload);
      if (accept_payload == nullptr) {
        return;
      }
      client_handler::process_accept_job(*accept_payload, connection_table_,
                                         *io_reactor_,
                                         worker_pool_->job_scheduler(), broker_,
                                         broker_config_);
      return;
    }
    case JobType::Decode:
      client_handler::process_decode_job(job.connection_fd, connection_table_,
                                         *io_reactor_,
                                         worker_pool_->job_scheduler(), broker_);
      return;
    case JobType::Drain:
      client_handler::process_drain_job(job.connection_fd, connection_table_,
                                        *io_reactor_,
                                        worker_pool_->job_scheduler(), broker_);
      return;
    case JobType::Close:
      client_handler::process_close_job(job.connection_fd, connection_table_,
                                        *io_reactor_, broker_);
      return;
    }
  } catch (...) {
    client_handler::process_close_job(job.connection_fd, connection_table_,
                                      *io_reactor_, broker_);
  }
}

} // namespace mqtt
