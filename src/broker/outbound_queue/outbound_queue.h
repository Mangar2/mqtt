#pragma once

/**
 * @file outbound_queue.h
 * @brief OutboundQueue — thread-safe per-client message queue (Module 20.1).
 */

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief Thread-safe FIFO queue of Message objects for outbound delivery
 *        (Module 20.1).
 *
 * Decouples the publishing thread (which calls `push()` via the broker's
 * delivery callback) from the receiving client's own thread (which calls
 * `try_pop()` to drain messages for QoS processing).
 *
 * ### Backpressure
 * When the queue reaches `max_depth`, `push()` drops the incoming message and
 * returns `false` (drop-on-overflow policy, 20.1.5).
 *
 * ### Shutdown
 * `stop()` marks the queue as terminated.  Subsequent `push()` calls return
 * `false` immediately.  `try_pop()` continues to drain remaining messages
 * until the queue is empty.
 *
 * Thread safety: all public methods are safe to call concurrently.
 */
class OutboundQueue {
public:
  /** @brief Default maximum queue depth (number of messages). */
  static constexpr std::size_t k_default_max_depth = 1000U;

  /**
   * @brief Construct a queue with a maximum depth.
   * @param max_depth Maximum number of messages the queue can hold.
   */
  explicit OutboundQueue(std::size_t max_depth = k_default_max_depth);

  /**
   * @brief Push a message into the queue (20.1.2).
   *
   * Called from any thread (the broker's delivery callback).
   * Non-blocking.
   *
   * @param msg Message to enqueue.
   * @return `true` on success; `false` when the queue is full or stopped.
   */
  [[nodiscard]] bool push(Message msg);

  /**
   * @brief Pop the next message from the queue (20.1.3).
   *
   * Non-blocking: returns `std::nullopt` when the queue is empty.
   * Intended for the client's own thread.
   *
   * @return The next message, or `std::nullopt` if the queue is empty.
   */
  [[nodiscard]] std::optional<Message> try_pop();

  /**
   * @brief Signal shutdown; reject future pushes (20.1.4).
   *
   * Safe to call from any thread.  Idempotent.
   */
  void stop();

  /**
   * @brief Return whether the queue has been stopped.
   * @return `true` after `stop()` has been called.
   */
  [[nodiscard]] bool is_stopped() const noexcept;

  /**
   * @brief Return whether the queue holds no messages.
   * @return `true` when the queue is empty.
   */
  [[nodiscard]] bool is_empty() const noexcept;

  /**
   * @brief Return the current number of queued messages.
   * @return Message count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  const std::size_t max_depth_;      ///< Upper bound on queue_.size().
  mutable std::mutex mutex_;         ///< Guards queue_.
  std::queue<Message> queue_;        ///< Pending outbound messages.
  std::atomic<bool> stopped_{false}; ///< True after stop() is called.
};

} // namespace mqtt
