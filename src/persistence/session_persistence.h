#pragma once

/**
 * @file session_persistence.h
 * @brief Crash-safe persistence adapter for MQTT sessions (Module 13.1).
 *
 * Each write replaces the entire sessions file atomically via `CrashSafeFile`.
 * On startup, `load_all()` reads the latest valid snapshot and returns all
 * persisted sessions; the caller is responsible for populating the in-memory
 * `SessionStore`.
 */

#include <filesystem>
#include <vector>

#include "data_model/session/session_state.h"

namespace mqtt {

/**
 * @brief Crash-safe persistence adapter for `SessionState` records
 * (Module 13.1).
 *
 * All sessions are stored together in a single `sessions.dat` file managed by
 * `CrashSafeFile`.  Every write replaces the full snapshot atomically so that
 * a crash during writing always leaves a recoverable previous state.
 *
 * Thread safety: none — external synchronisation required.
 */
class SessionPersistence {
public:
  /**
   * @brief Construct a SessionPersistence adapter.
   * @param dir  Directory where the session snapshot file is stored.
   *             Created on the first write if it does not exist.
   */
  explicit SessionPersistence(std::filesystem::path dir);

  /**
   * @brief Persist the complete set of sessions atomically (13.1.1).
   *
   * Replaces the previous snapshot.  Pass the full list of currently live
   * sessions each time a session is added, updated, or removed.
   *
   * @param sessions  All sessions to persist.
   * @throws PersistenceException on I/O failure.
   */
  void save_all(const std::vector<SessionState> &sessions);

  /**
   * @brief Load all sessions from the latest valid snapshot (13.1.2).
   *
   * Returns an empty vector when no valid snapshot exists (first boot or
   * unrecoverable corruption).
   *
   * @return All persisted sessions.
   * @throws PersistenceException on unexpected I/O failure.
   */
  [[nodiscard]] std::vector<SessionState> load_all() const;

private:
  std::filesystem::path dir_; ///< Directory holding the snapshot file.
};

} // namespace mqtt
