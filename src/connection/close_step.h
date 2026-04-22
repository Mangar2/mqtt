#pragma once

/**
 * @file close_step.h
 * @brief Final broker close handling for one connection session.
 */

namespace mqtt {

class Broker;
class ConnectionSession;

void finalize_close(ConnectionSession &session, Broker &broker);

} // namespace mqtt
