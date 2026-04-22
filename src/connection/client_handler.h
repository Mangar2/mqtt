#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler — lean per-connection I/O orchestrator (Module 24).
 */

#include <chrono>
#include <memory>

#include "executor/connection_job.h"

namespace mqtt {

class Broker;
class ConnectionTable;
class IoReactor;
class JobScheduler;
struct BrokerConfig;
class TcpConnection;

namespace client_handler {

void process_accept_job(const AcceptJobPayload &payload, ConnectionTable &table,
                        IoReactor &reactor, JobScheduler &scheduler,
                        Broker &broker, const BrokerConfig &config);

void process_decode_job(int fd, ConnectionTable &table, IoReactor &reactor,
                        JobScheduler &scheduler, Broker &broker);

void process_drain_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker);

void process_close_job(int fd, ConnectionTable &table, IoReactor &reactor,
                       Broker &broker);

} // namespace client_handler

/**
 * @brief Thin per-connection orchestrator that handles transport I/O only.
 *
 * `ClientHandler` owns no broker business logic. It reads packets from the
 * transport, delegates workflow to Broker facades and ClientSession handlers,
 * and enqueues encoded responses for asynchronous socket writes.
 */
class ClientHandler {
public:
  /**
   * @brief Handle one accepted client connection until teardown.
   *
   * @param conn Accepted TCP connection.
   * @param broker Running broker instance.
   * @param config Broker runtime configuration.
   * @param is_ws True when the listener is MQTT-over-WebSocket.
   */
  void run(std::unique_ptr<TcpConnection> conn, Broker &broker,
           const BrokerConfig &config, bool is_ws);

private:
  std::chrono::steady_clock::time_point last_run_started_;
};

} // namespace mqtt
