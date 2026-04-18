#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler — lean per-connection I/O orchestrator (Module 24).
 */

#include <memory>

namespace mqtt {

class Broker;
struct BrokerConfig;
class TcpConnection;

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
};

} // namespace mqtt
