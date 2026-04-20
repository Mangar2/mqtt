#pragma once

/**
 * @file active_connection_registry.h
 * @brief Thread-safe registry for online client outbound queues.
 */

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "outbound_queue/outbound_queue.h"

namespace mqtt {

/**
 * @brief Result of inserting or replacing an online client queue.
 */
struct ConnectionUpsertResult {
  bool replaced_existing{false};
  std::size_t active_connections{0U};
  std::shared_ptr<OutboundQueue> previous_queue;
};

/**
 * @brief Result of conditional remove operation.
 */
struct ConnectionRemoveResult {
  bool removed{false};
  std::size_t active_connections_before{0U};
  std::shared_ptr<OutboundQueue> removed_queue;
};

/**
 * @brief Thread-safe online connection registry.
 *
 * Owns the mapping `client_id -> outbound queue` and serializes all map
 * operations internally via a shared mutex.
 */
class ActiveConnectionRegistry {
public:
  [[nodiscard]] ConnectionUpsertResult
  upsert(std::string_view client_id, std::shared_ptr<OutboundQueue> queue);

  [[nodiscard]] ConnectionRemoveResult
  remove_if_matches(std::string_view client_id,
                    const std::shared_ptr<OutboundQueue> &expected_queue);

  [[nodiscard]] std::shared_ptr<OutboundQueue>
  find(std::string_view client_id) const;

  [[nodiscard]] bool contains(std::string_view client_id) const;

  [[nodiscard]] std::size_t size() const noexcept;

  [[nodiscard]] std::vector<std::shared_ptr<OutboundQueue>>
  snapshot_queues() const;

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<OutboundQueue>>
      active_connections_;
};

} // namespace mqtt
