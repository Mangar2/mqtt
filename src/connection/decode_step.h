#pragma once

/**
 * @file decode_step.h
 * @brief Decodes at most one packet from one connection session.
 */

namespace mqtt {

class Broker;
class ConnectionSession;

enum class DecodeOutcome {
  NeedMore,
  Processed,
  ProtocolError,
  Disconnected,
};

[[nodiscard]] DecodeOutcome decode_one_packet(ConnectionSession &session,
                                              Broker &broker);

} // namespace mqtt
