#pragma once

/**
 * @file inflight_persistence.h
 * @brief Crash-safe persistence adapter for in-flight QoS entries
 * (Module 13.3).
 *
 * All in-flight entries for all sessions are stored together in a single
 * `inflight.dat` file managed by `CrashSafeFile`.
 *
 * **Timestamp restoration:** `std::chrono::steady_clock::time_point` values
 * cannot survive a reboot (steady_clock is relative to an unspecified epoch).
 * On load, every restored timestamp is replaced with the current
 * `steady_clock::now()` so that the QoS Engine treats every restored entry
 * as immediately due for retransmission — the safest correct behaviour.
 */

#include <filesystem>
#include <string>
#include <vector>

#include "data_model/session/inflight_entry.h"

namespace mqtt {

/**
 * @brief Crash-safe persistence adapter for `InflightEntry` records
 * (Module 13.3).
 *
 * Thread safety: none — external synchronisation required.
 */
class InflightPersistence {
public:
  /**
   * @brief Construct an InflightPersistence adapter.
   * @param dir  Directory where the inflight snapshot file is stored.
   */
  explicit InflightPersistence(std::filesystem::path dir);

  /**
   * @brief A flat record associating a client ID with one inflight entry.
   *
   * Used as the element type for `save_all` / `load_all` to avoid a nested
   * map structure in the persistence layer.
   */
  struct ClientEntry {
    std::string client_id; ///< Owning client identifier.
    InflightEntry entry;   ///< In-flight handshake record.
  };

  /**
   * @brief Persist all inflight entries for all sessions atomically (13.3.1).
   *
   * @param entries  All (client_id, InflightEntry) pairs to persist.
   * @throws PersistenceException on I/O failure.
   */
  void save_all(const std::vector<ClientEntry> &entries);

  /**
   * @brief Load all inflight entries from the latest valid snapshot (13.3.2).
   *
   * Timestamps are restored as `steady_clock::now()` — see class doc.
   * Returns an empty vector when no valid snapshot exists.
   *
   * @return All (client_id, InflightEntry) pairs from the snapshot.
   * @throws PersistenceException on unexpected I/O failure.
   */
  [[nodiscard]] std::vector<ClientEntry> load_all() const;

private:
  std::filesystem::path dir_; ///< Directory holding the snapshot file.
};

} // namespace mqtt
