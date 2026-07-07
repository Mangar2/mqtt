#pragma once

/**
 * @file decode_step.h
 * @brief Decodes at most one packet from one connection session.
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
 * @brief Result classification for one decode step.
 */
enum class DecodeOutcome {
  NeedMore,
  Processed,
  ProtocolError,
  Disconnected,
};

[[nodiscard]] DecodeOutcome decode_one_packet(ConnectionSession &session,
                                              Broker &broker);

} // namespace mqtt
