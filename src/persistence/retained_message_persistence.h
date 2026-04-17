#pragma once

/**
 * @file retained_message_persistence.h
 * @brief Crash-safe persistence adapter for retained MQTT messages
 * (Module 13.2).
 *
 * All retained messages are stored together in a single `retained.dat` file
 * managed by `CrashSafeFile`.  Every write replaces the full snapshot
 * atomically.
 */

#include <filesystem>
#include <vector>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief Crash-safe persistence adapter for retained `Message` records
 * (Module 13.2).
 *
 * Thread safety: none — external synchronisation required.
 */
class RetainedMessagePersistence {
public:
  /**
   * @brief Construct a RetainedMessagePersistence adapter.
   * @param dir  Directory where the retained message snapshot file is stored.
   */
  explicit RetainedMessagePersistence(std::filesystem::path dir);

  /**
   * @brief Persist the complete set of retained messages atomically (13.2.1).
   *
   * @param messages  All currently retained messages.
   * @throws PersistenceException on I/O failure.
   */
  void save_all(const std::vector<Message> &messages);

  /**
   * @brief Load all retained messages from the latest valid snapshot (13.2.2).
   *
   * Returns an empty vector when no valid snapshot exists.
   *
   * @return All persisted retained messages.
   * @throws PersistenceException on unexpected I/O failure.
   */
  [[nodiscard]] std::vector<Message> load_all() const;

private:
  std::filesystem::path dir_; ///< Directory holding the snapshot file.
};

} // namespace mqtt
