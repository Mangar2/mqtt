#pragma once

/**
 * @file will_store.h
 * @brief In-memory Will Message store (Module 11.1).
 */

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief In-memory store for MQTT 5.0 Will Messages (Module 11.1).
 *
 * Keyed by Client ID.  Entries are created when a client connects with a will
 * field in its CONNECT packet, and removed either after the will is published
 * or when the client disconnects normally (Reason Code 0x00).
 *
 * Thread safety: none — external synchronisation required.
 */
class WillStore {
public:
  /**
   * @brief Persist (or replace) the Will Message for a client (11.1.1).
   *
   * If an entry already exists for @p client_id it is overwritten.
   *
   * @param client_id Client identifier.
   * @param will      Will Message to store.
   */
  void store(std::string_view client_id, const WillMessage &will);

  /**
   * @brief Load the Will Message for a client (11.1.2).
   *
   * @param client_id Client identifier to look up.
   * @return `WillMessage` if found, `std::nullopt` otherwise.
   */
  [[nodiscard]] std::optional<WillMessage> load(std::string_view client_id) const;

  /**
   * @brief Delete the Will Message for a client (11.1.3).
   *
   * No-op when @p client_id is not present.
   *
   * @param client_id Client identifier whose will is to be removed.
   */
  void remove(std::string_view client_id);

  /**
   * @brief Return the number of stored Will Message entries.
   * @return Entry count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, WillMessage>
      wills_; ///< Will entries keyed by client ID.
};

} // namespace mqtt
