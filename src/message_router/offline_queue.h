#pragma once

/**
 * @file offline_queue.h
 * @brief OfflineQueue — per-client message buffer for disconnected sessions
 *        (Module 12.3).
 */

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/message/message.h"

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief A message together with the time it was enqueued.
 *
 * Used by MessageExpiryController to compute the remaining
 * Message Expiry Interval before delivering a queued message.
 */
struct QueuedMessage {
  Message message; ///< Ready-to-deliver message (subscription rules already
                   ///< applied).
  std::chrono::steady_clock::time_point
      enqueue_time; ///< Wall-clock instant when the message was queued.
};

/**
 * @brief Per-client FIFO message buffer for disconnected sessions
 * (Module 12.3).
 *
 * When a subscriber is offline, messages destined for it are stored here
 * (12.3.1).  Upon reconnect the caller drains the queue and delivers the
 * buffered messages (12.3.2).  A configurable maximum queue depth prevents
 * unbounded memory growth: when the limit is reached, new messages are
 * discarded and a QueueFull exception is thrown (12.3.3).
 *
 * Thread safety: internally synchronized via `std::mutex`.
 */
class OfflineQueue {
public:
  /// Default per-client message queue depth limit.
  static constexpr std::size_t k_default_max_size = 100U;

  /**
   * @brief Construct an OfflineQueue.
   * @param max_size Maximum number of buffered messages per client.
   *                 Defaults to k_default_max_size.
   */
  explicit OfflineQueue(std::size_t max_size = k_default_max_size) noexcept;

  /**
   * @brief Enqueue a message for a disconnected client (12.3.1).
   *
   * The enqueue timestamp is recorded as `std::chrono::steady_clock::now()`.
   *
   * @param client_id Identifier of the target client session.
   * @param msg       Outbound message (subscription rules already applied).
   * @throws MessageRouterException(QueueFull) when the client's queue has
   *         reached max_size.
   */
  void enqueue(std::string_view client_id, const Message &msg);

  /**
   * @brief Enqueue a message and drop the current oldest when queue is full.
   *
   * Used for policies where the newest message should be kept even when the
   * per-client queue reached @ref max_size_.
   *
   * @param client_id Identifier of the target client session.
   * @param msg       Outbound message (subscription rules already applied).
   */
  void enqueue_drop_oldest(std::string_view client_id, const Message &msg);

  /**
   * @brief Drain and return all queued messages for a client (12.3.2).
   *
   * The client's queue is cleared on return.  Returns an empty vector when
   * no messages are queued for @p client_id.
   *
   * @param client_id Identifier of the client that has reconnected.
   * @return All buffered QueuedMessage values in FIFO order.
   */
  [[nodiscard]] std::vector<QueuedMessage> drain(std::string_view client_id);

  /**
   * @brief Return the number of buffered messages for a client.
   * @param client_id Identifier of the client to query.
   * @return Buffered message count; 0 when no queue exists for this client.
   */
  [[nodiscard]] std::size_t size(std::string_view client_id) const noexcept;

  /**
   * @brief Remove all buffered messages for a client without delivering them.
   *
   * Called when a session is deleted or expires.
   *
   * @param client_id Identifier of the client whose queue is purged.
   */
  void purge(std::string_view client_id);

  /**
   * @brief Return a snapshot of all non-empty per-client queues (13.4).
   *
   * Only clients with at least one queued message are included.
   * Used by the persistence layer to atomically snapshot the offline queue.
   *
   * @return Map from client_id to the ordered list of queued messages.
   */
  [[nodiscard]] std::unordered_map<std::string, std::vector<Message>> snapshot() const;

  /**
   * @brief Restore queued messages for one client from persisted data (13.4).
   *
   * Replaces any existing queue content for @p client_id.
   * The enqueue timestamp is set to `steady_clock::now()` so that
   * MessageExpiryController does not immediately expire restored messages.
   *
   * @param client_id Identifier of the target client.
   * @param messages  Messages to restore in FIFO order.
   */
  void restore(std::string_view client_id, std::vector<Message> messages);

private:
  mutable std::mutex mutex_;
  std::size_t max_size_; ///< Per-client queue depth limit.
  std::unordered_map<std::string, std::deque<QueuedMessage>>
      queues_; ///< Per-client FIFO queues.
};

} // namespace mqtt
