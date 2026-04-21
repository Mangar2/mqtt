#pragma once

/**
 * @file offline_queue_persistence.h
 * @brief Crash-safe persistence adapter for offline message queues
 * (Module 13.4).
 *
 * All offline queue entries for all sessions are stored together in a single
 * `offline_queue.dat` file managed by `CrashSafeFile`.
 *
 * **Enqueue-time restoration:** `std::chrono::steady_clock::time_point` values
 * cannot survive a reboot.  On load, every restored enqueue timestamp is
 * replaced with `steady_clock::now()` so that MessageExpiryController treats
 * restored messages as freshly queued — avoiding spurious expiry.
 */

#include <filesystem>
#include <string>
#include <vector>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief Crash-safe persistence adapter for offline message queue entries
 * (Module 13.4).
 *
 * Serialises each (client_id, Message) pair as one flat record so that the
 * complete offline queue state for all sessions can be atomically snapshotted
 * and recovered.
 *
 * Thread safety: none — external synchronisation required.
 */
class OfflineQueuePersistence {
public:
  /**
   * @brief A flat record grouping all queued messages for one client.
   *
   * Used as the element type for `save_all` / `load_all` to avoid a nested
   * map structure in the persistence layer.
   */
  struct ClientMessages {
    std::string client_id;       ///< Owning client identifier.
    std::vector<Message> messages; ///< Queued messages in FIFO order.
  };

  /**
   * @brief Construct an OfflineQueuePersistence adapter.
   * @param dir Directory where the offline queue snapshot file is stored.
   */
  explicit OfflineQueuePersistence(std::filesystem::path dir);

  /**
   * @brief Persist all offline queue entries for all sessions atomically
   * (13.4.1).
   *
   * @param entries All (client_id, messages) groups to persist.
   * @throws PersistenceException on I/O failure.
   */
  void save_all(const std::vector<ClientMessages> &entries);

  /**
   * @brief Load all offline queue entries from the latest valid snapshot
   * (13.4.2).
   *
   * Enqueue timestamps are restored as `steady_clock::now()` — see class doc.
   * Returns an empty vector when no valid snapshot exists.
   *
   * @return All (client_id, messages) groups from the snapshot.
   * @throws PersistenceException on unexpected I/O failure.
   */
  [[nodiscard]] std::vector<ClientMessages> load_all() const;

private:
  std::filesystem::path dir_; ///< Directory holding the snapshot file.
};

} // namespace mqtt
