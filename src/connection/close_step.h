#pragma once

/**
 * @file close_step.h
 * @brief Final broker close handling for one connection session.
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
 * @brief Finalize close flow for one connection session.
 * @param session Connection session being closed.
 * @param broker Broker facade used for cleanup side effects.
 */
void finalize_close(ConnectionSession &session, Broker &broker);

} // namespace mqtt
