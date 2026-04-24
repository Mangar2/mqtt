#pragma once

/**
 * @file runtime_step.h
 * @brief Single-step runtime packet processing for one decoded packet.
 */

#include "codec/packet_reader/packet_reader.h"

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
 * @brief Result classification for one runtime packet step.
 */
enum class RuntimeOutcome {
  Continuing,
  DisconnectClean,
  DisconnectError,
};

[[nodiscard]] RuntimeOutcome
process_runtime_packet(ConnectionSession &session, Broker &broker,
                       const AnyPacket &packet);

} // namespace mqtt
