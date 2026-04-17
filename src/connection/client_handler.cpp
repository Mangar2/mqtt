/**
 * @file client_handler.cpp
 * @brief ClientHandler placeholder implementation.
 */

#include "connection/client_handler.h"

#include "network/tcp_connection.h"

namespace mqtt {

void ClientHandler::run(std::unique_ptr<TcpConnection> conn, Broker &broker,
                        const BrokerConfig &config, bool is_ws) {
  (void)this;
  (void)broker;
  (void)config;
  (void)is_ws;

  if (conn) {
    conn->close();
  }
}

} // namespace mqtt
