#pragma once

/**
 * @file inflight_store.h
 * @brief In-memory inflight entry store (Module 4.4).
 */

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"

namespace mqtt {

/**
 * @brief In-memory store for QoS 1 and QoS 2 in-flight message records
 * (Module 4.4).
 *
 * Entries are keyed by `(client_id, packet_id, direction)`.  One entry
 * represents one pending acknowledgement in the handshake state machine
 * (Module 5).
 *
 * Thread safety: all public methods are internally synchronized.
 */
class InflightStore {
public:
  /**
   * @brief Add a new inflight entry for a session (4.4.1).
   *
   * @param client_id Identifier of the owning session.
   * @param entry     Entry to store; `entry.packet_id` must be non-zero and
   *                  unique within `(client_id, entry.direction)`.
   */
  void create(std::string_view client_id, const InflightEntry &entry);

  /**
   * @brief Advance the handshake state of an existing entry (4.4.2).
   *
   * @param client_id  Identifier of the owning session.
   * @param packet_id  Packet Identifier of the entry to update.
   * @param direction  Direction of the exchange.
   * @param new_state  New handshake state to set.
   * @throws StoreException(PacketIdNotFound) if no matching entry exists.
   */
  void update(std::string_view client_id, uint16_t packet_id,
              InflightDirection direction, InflightState new_state);

  /**
   * @brief Remove a completed inflight entry (4.4.3).
   *
   * Does nothing when no matching entry is found.
   *
   * @param client_id Identifier of the owning session.
   * @param packet_id Packet Identifier of the entry to remove.
   * @param direction Direction of the exchange.
   */
  void remove(std::string_view client_id, uint16_t packet_id,
              InflightDirection direction);

  /**
   * @brief Return a copy of all inflight entries for a session (4.4.4).
   *
   * @param client_id Identifier of the session to query.
   * @return Vector of all `InflightEntry` values; empty if the session has
   * none.
   */
  [[nodiscard]] std::vector<InflightEntry> entries_for(std::string_view client_id) const;

  /**
   * @brief Check whether a packet ID is currently registered (4.4.5).
   *
   * @param client_id Identifier of the session.
   * @param packet_id Packet Identifier to test.
   * @param direction Direction of the exchange.
   * @return `true` if an entry with the given `(packet_id, direction)` exists.
   */
  [[nodiscard]] bool
  is_packet_id_in_use(std::string_view client_id, uint16_t packet_id,
                      InflightDirection direction) const noexcept;

  /**
   * @brief Return the number of inflight entries for a session.
   * @param client_id Identifier of the session to query.
   * @return Entry count; zero when the session has no inflight messages.
   */
  [[nodiscard]] std::size_t size_for(std::string_view client_id) const noexcept;

  /**
   * @brief Return the number of inflight entries across all sessions.
   * @return Total inflight entry count.
   */
  [[nodiscard]] std::size_t total_size() const noexcept;

private:
  /// Guards all accesses to @ref entries_.
  mutable std::mutex mutex_;

  /// Per-session list of in-flight entries.
  std::unordered_map<std::string, std::vector<InflightEntry>> entries_;

  /**
   * @brief Find an entry by packet_id and direction within a session's list.
   *
   * @param list      Reference to the per-session entry vector.
   * @param packet_id Packet Identifier to find.
   * @param direction Direction to match.
   * @return Iterator to the matching element, or `list.end()` if not found.
   */
  [[nodiscard]] static std::vector<InflightEntry>::iterator
  find_entry(std::vector<InflightEntry> &list, uint16_t packet_id,
             InflightDirection direction) noexcept;
};

} // namespace mqtt
