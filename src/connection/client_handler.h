#pragma once

/**
 * @file client_handler.h
 * @brief ClientHandler — runs the full MQTT 5.0 per-client session loop
 *        (Module 17).
 */

#include <memory>

#include "broker/broker_config.h"
#include "network/tcp_connection.h"

namespace mqtt {

class Broker;

/**
 * @brief Runs the complete MQTT 5.0 connection lifecycle for one TCP
 *        connection on a dedicated thread (Module 17).
 *
 * Sequence executed by `run()`:
 * 1. Optional WebSocket upgrade handshake when `is_ws == true` (17.1.2).
 * 2. Set SO_RCVTIMEO on the socket for keep-alive polling (17.2, 17.5.1).
 * 3. Read the first complete packet; reject any packet that is not CONNECT
 *    with a protocol error DISCONNECT (17.2.1).
 * 4. Authenticate credentials via `IAuthenticator` / `EnhancedAuthHandler`
 *    (17.2.2).
 * 5. Open or resume the session via `SessionManager` (17.2.3).
 * 6. Store the Will Message if present (11.1.1).
 * 7. Send CONNACK with the negotiated session state (17.2.4).
 * 8. Register the `SendFn` callback with the `Broker` (17.4.1).
 * 9. Start a drain thread that writes queued bytes to the socket (17.4.4).
 * 10. Flush the offline message queue for resumed sessions (17.5.3).
 * 11. Enter the per-packet dispatch loop (17.3):
 *     - Reset the Keep-Alive timer on every packet (17.3.2).
 *     - Dispatch each packet type to the appropriate handler.
 *     - Check the Keep-Alive deadline periodically (17.5.1).
 * 12. Teardown: unregister connection, handle will, handle session
 *     disconnect (17.5).
 *
 * Thread safety: each `run()` call must execute on its own thread;
 * concurrent calls on the same `ClientHandler` instance are not safe.
 */
class ClientHandler {
public:
  /**
   * @brief Run the complete lifecycle for one accepted TCP connection.
   *
   * Blocks until the connection is closed (either by the client, the
   * keep-alive timer, a protocol error, or broker shutdown).
   *
   * @param conn   Accepted TCP connection — ownership transferred.
   * @param broker Broker that owns all shared modules.
   * @param config Broker configuration — limits, feature flags, timeouts.
   * @param is_ws  `true` when the connection arrived on the WebSocket port
   *               and a WebSocket upgrade must be completed first (17.1.2).
   */
  void run(std::unique_ptr<TcpConnection> conn, Broker &broker,
           const BrokerConfig &config, bool is_ws);
};

} // namespace mqtt
