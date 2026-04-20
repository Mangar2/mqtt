#pragma once

/**
 * @file connection_table.h
 * @brief Thread-safe table of ConnectionSlot instances indexed by socket fd.
 */

#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include "network/connection_slot.h"

namespace mqtt {

/**
 * @brief Owns all active connection slots and provides fd-indexed lookup.
 */
class ConnectionTable {
public:
  /**
   * @brief Insert a new slot.
   * @param slot Slot instance to insert.
   * @return True on success, false when an entry for this fd already exists.
   */
  [[nodiscard]] bool add(ConnectionSlot slot);

  /**
   * @brief Remove a slot by fd.
   * @param socket_fd Socket file descriptor.
   * @return True when an entry was removed.
   */
  [[nodiscard]] bool remove(int socket_fd);

  /**
   * @brief Find a slot by fd.
   * @param socket_fd Socket file descriptor.
   * @return Pointer to slot or nullptr when not found.
   */
  [[nodiscard]] ConnectionSlot *find(int socket_fd);

  /**
   * @brief Find a slot by fd.
   * @param socket_fd Socket file descriptor.
   * @return Const pointer to slot or nullptr when not found.
   */
  [[nodiscard]] const ConnectionSlot *find(int socket_fd) const;

  /**
   * @brief Return current number of tracked slots.
   * @return Number of entries.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<int, std::unique_ptr<ConnectionSlot>> slots_;
};

} // namespace mqtt

