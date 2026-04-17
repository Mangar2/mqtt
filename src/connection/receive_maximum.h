#pragma once

/**
 * @file receive_maximum.h
 * @brief ReceiveMaximum — inflight QoS 1/2 packet flow-control for a single
 * connection (Module 7.4).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief Controls the number of simultaneously inflight QoS 1 and QoS 2 packets
 *        on a single outbound connection (Module 7.4).
 *
 * The MQTT 5.0 Receive Maximum property (§3.1.2.11.4, §3.2.2.3.3) limits how
 * many unacknowledged QoS 1/2 PUBLISH packets the sender may have outstanding
 * at any time.
 *
 * ### Flow control (7.4.1 – 7.4.3)
 * - `acquire()` is called before sending each outbound QoS 1/2 PUBLISH.
 *   Returns `false` (pause signal) when the limit is reached (7.4.2).
 * - `release()` is called when the corresponding acknowledgement (PUBACK or
 *   PUBCOMP) is received, freeing one slot for a new PUBLISH (7.4.3).
 *
 * Thread safety: none — external synchronisation required.
 */
class ReceiveMaximum {
public:
  /**
   * @brief Construct with the negotiated Receive Maximum.
   *
   * @param max Maximum number of simultaneously inflight QoS 1/2 PUBLISH
   * packets. Per the MQTT 5.0 specification the value must be in [1, 65535];
   *            passing 0 is treated as 65535 (no effective limit) to match the
   *            default broker behaviour.
   */
  explicit ReceiveMaximum(uint16_t max) noexcept;

  /**
   * @brief Attempt to acquire one inflight slot before sending a QoS 1/2
   * PUBLISH (7.4.1).
   *
   * @return `true` if a slot was acquired (send may proceed);
   *         `false` if the limit is reached (sending must pause) (7.4.2).
   */
  [[nodiscard]] bool acquire() noexcept;

  /**
   * @brief Release one inflight slot after receiving an ACK (7.4.3).
   *
   * @throws ConnectionException(InvalidState) if the inflight count is already
   * 0.
   */
  void release();

  /**
   * @brief Return true when the inflight count equals the maximum (sending is
   * paused) (7.4.2).
   * @return `true` if no more outbound PUBLISH packets may be sent.
   */
  [[nodiscard]] bool is_paused() const noexcept;

  /**
   * @brief Return the number of additional PUBLISH slots available.
   * @return Remaining capacity in [0, max].
   */
  [[nodiscard]] uint16_t available() const noexcept;

  /**
   * @brief Return the configured maximum.
   * @return Receive Maximum value supplied at construction.
   */
  [[nodiscard]] uint16_t max() const noexcept;

private:
  uint16_t max_;          ///< Negotiated Receive Maximum.
  uint16_t inflight_{0U}; ///< Currently outstanding unacknowledged packets.
};

} // namespace mqtt
