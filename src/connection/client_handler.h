#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler placeholder.
 */

#include <memory>

namespace mqtt {

class Broker;
struct BrokerConfig;
class TcpConnection;

/**
 * @brief Temporary connection handler stub.
 *
 * The full Module 17 implementation is intentionally postponed.
 * This placeholder closes accepted connections immediately so unfinished
 * protocol logic does not interfere with ongoing development.
 */
class ClientHandler {
public:
  /**
   * @brief Handle one accepted connection.
   *
   * Current behavior: close the socket and return.
   *
   * @param conn Accepted TCP connection.
   * @param broker Unused for now.
   * @param config Unused for now.
   * @param is_ws Unused for now.
   */
  void run(std::unique_ptr<TcpConnection> conn, Broker &broker,
           const BrokerConfig &config, bool is_ws);
};

} // namespace mqtt
