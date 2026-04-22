#pragma once

/**
 * @file handshake_step.h
 * @brief Single-step CONNECT/AUTH processing for one decoded packet.
 */

#include "codec/packet_reader/packet_reader.h"

namespace mqtt {

class Broker;
class ConnectionSession;

enum class HandshakeOutcome {
  Continuing,
  ConnectAccepted,
  Rejected,
};

[[nodiscard]] HandshakeOutcome
process_handshake_packet(ConnectionSession &session, Broker &broker,
                         const AnyPacket &packet);

} // namespace mqtt
