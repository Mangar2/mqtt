#pragma once

/**
 * @file packet_id_manager.h
 * @brief Per-session Packet Identifier allocator (Module 5.1).
 */

#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "data_model/session/inflight_direction.h"

namespace mqtt {

/**
 * @brief Per-session Packet Identifier allocator and registry (Module 5.1).
 *
 * Manages two independent ID spaces — one for outbound (broker-initiated) and
 * one for inbound (client-initiated) QoS 1 / QoS 2 exchanges — so that the
 * same numeric value may simultaneously exist in both spaces without conflict.
 *
 * ### Outbound IDs (5.1.1)
 * The broker calls `allocate()` to obtain the next free ID before starting a
 * new outbound PUBLISH exchange. IDs are scanned sequentially from
 * `last_id_ + 1` (wrapping 65535 → 1), giving O(1) amortised allocation.
 *
 * ### Inbound IDs (5.1.3)
 * Client-chosen IDs arrive with a PUBLISH. `try_register_inbound()` marks an
 * ID as active and detects QoS 2 duplicates: if the ID is already present the
 * PUBLISH is a retransmission and must not be re-delivered.
 *
 * Both spaces are cleared by successive `release()` calls; there is no bulk
 * reset (a session restart creates a new `PacketIdManager`).
 *
 * Thread safety: none — external synchronisation required.
 */
class PacketIdManager {
public:
  /**
   * @brief Allocate the next free outbound Packet ID (5.1.1).
   *
   * Scans [1, 65535] starting one past `last_id_` and wrapping around.
   *
   * @return Non-zero ID in [1, 65535] not currently in the outbound set.
   * @throws QosException(PacketIdExhausted) if all 65535 IDs are in use.
   */
  [[nodiscard]] uint16_t allocate();

  /**
   * @brief Register a client-chosen inbound Packet ID for duplicate detection.
   *
   * Used by the QoS 2 inbound state machine when a PUBLISH arrives.
   *
   * @param id Non-zero Packet ID supplied by the publishing client.
   * @return `true` if the ID was new and has been registered;
   *         `false` if the ID was already registered (duplicate PUBLISH).
   */
  [[nodiscard]] bool try_register_inbound(uint16_t pid);

  /**
   * @brief Release a Packet ID back to the free pool (5.1.2).
   *
   * No-op when the ID is not registered in the given direction.
   *
   * @param pid Packet ID to release.
   * @param dir Direction space to release from (5.1.3).
   */
  void release(uint16_t pid, InflightDirection dir) noexcept;

  /**
   * @brief Test whether a Packet ID is currently in use for a direction
   * (5.1.3).
   * @param pid Packet ID to test.
   * @param dir Direction space to check.
   * @return `true` if registered; `false` otherwise.
   */
  [[nodiscard]] bool is_in_use(uint16_t pid,
                               InflightDirection dir) const noexcept;

  /**
   * @brief Number of active outbound Packet IDs.
   * @return Count in [0, 65535].
   */
  [[nodiscard]] std::size_t outbound_count() const noexcept;

  /**
   * @brief Number of active inbound Packet IDs.
   * @return Count in [0, 65535].
   */
  [[nodiscard]] std::size_t inbound_count() const noexcept;

private:
  std::unordered_set<uint16_t>
      inbound_ids_; ///< Currently registered inbound IDs.
  std::unordered_set<uint16_t>
      outbound_ids_;    ///< Currently allocated outbound IDs.
  uint16_t last_id_{0}; ///< Scan cursor for outbound allocation.
};

} // namespace mqtt
