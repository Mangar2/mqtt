#pragma once

/**
 * @file outbound_drain_step.h
 * @brief Drains pending client outbound messages into encoded frame storage.
 */

namespace mqtt {

class Broker;
class ConnectionSession;

void drain_outbound_to_write_buffer(ConnectionSession &session, Broker &broker);

} // namespace mqtt
