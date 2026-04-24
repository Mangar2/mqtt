#pragma once

/**
 * @file outbound_drain_step.h
 * @brief Drains pending client outbound messages into encoded frame storage.
 */

namespace mqtt {

/**
 * @brief Forward declaration of Broker.
 */
class Broker;
/**
 * @brief Forward declaration of ConnectionSession.
 */
class ConnectionSession;

/**
 * @brief Drain routed outbound messages into connection write-frame buffer.
 * @param session Connection session to drain into.
 * @param broker Broker instance used for routing interactions.
 */
void drain_outbound_to_write_buffer(ConnectionSession &session, Broker &broker);

} // namespace mqtt
