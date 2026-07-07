#pragma once

/**
 * @file qos_error.h
 * @brief Error types for the QoS Engine module (Module 5).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes produced by the QoS Engine (Module 5).
 */
enum class QosError : uint8_t {
  PacketIdExhausted,  ///< All 65535 outbound Packet IDs are currently in use.
  UnexpectedPacketId, ///< An ACK was received for a Packet ID not in the
                      ///< inflight store.
  InvalidPacket, ///< A packet is missing a required field (e.g. absent Packet
                 ///< ID for QoS > 0).
};

/**
 * @brief Exception thrown by the QoS Engine on protocol or state violations.
 *
 * Derives from `std::runtime_error`. Callers use `error()` to branch on the
 * specific failure code without parsing the human-readable message.
 */
class QosException : public std::runtime_error {
public:
  /**
   * @brief Construct a QosException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description.
   */
  explicit QosException(QosError err, const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The `QosError` that caused this exception.
   */
  [[nodiscard]] QosError error() const noexcept { return error_; }

private:
  QosError error_; ///< Error code stored for programmatic inspection.
};

} // namespace mqtt
