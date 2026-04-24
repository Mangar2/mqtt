#pragma once

/**
 * @file connection_table.h
 * @brief Thread-safe table of ConnectionSlot instances indexed by socket fd.
 */

#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "connection/connection_session.h"
#include "network/connection_slot.h"

namespace mqtt {

/**
 * @brief Owns all active connection slots and provides fd-indexed lookup.
 */
class ConnectionTable {
public:
  /**
   * @brief Connection table entry containing slot and session.
   */
  struct Entry {
    ConnectionSlot slot;
    std::unique_ptr<ConnectionSession> session;
  };

  /**
   * @brief Insert a new slot.
   * @param socket_fd Socket file descriptor key.
   * @param slot Slot instance to insert.
   * @param session Per-connection session object owned by the table.
   * @return True on success, false when an entry for this fd already exists.
   */
  [[nodiscard]] bool add(int socket_fd, ConnectionSlot slot,
                         std::unique_ptr<ConnectionSession> session);

  /**
   * @brief Remove a slot by fd.
   * @param socket_fd Socket file descriptor.
   * @return True when an entry was removed.
   */
  [[nodiscard]] bool remove(int socket_fd);

  /**
   * @brief Find a slot by fd.
   * @param socket_fd Socket file descriptor.
  * @return Pointer to entry or nullptr when not found.
   */
  [[nodiscard]] Entry *find(int socket_fd);

  /**
   * @brief Find a slot by fd.
   * @param socket_fd Socket file descriptor.
  * @return Const pointer to entry or nullptr when not found.
   */
  [[nodiscard]] const Entry *find(int socket_fd) const;

  /**
  * @brief Remove all entries.
  */
  void clear();

  /**
   * @brief Return current number of tracked slots.
   * @return Number of entries.
   */
  [[nodiscard]] std::size_t size() const noexcept;

  /**
   * @brief Snapshot all currently tracked socket handles.
   * @return Vector of socket handles for active slots.
   */
  [[nodiscard]] std::vector<SocketHandle> snapshot_socket_handles() const;

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<int, std::unique_ptr<Entry>> entries_;
};

} // namespace mqtt

