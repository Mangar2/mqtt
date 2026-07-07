#pragma once

/**
 * @file job_queue.h
 * @brief Concurrent FIFO queue for ConnectionJob items.
 */

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

#include "executor/connection_job.h"

namespace mqtt {

/**
 * @brief Blocking producer/consumer queue for connection jobs.
 *
 * Thread safety: push/pop/size/shutdown are safe from multiple threads.
 */
class JobQueue {
public:
  /**
   * @brief Push one job into the queue.
   * @param job Job to enqueue.
   */
  void push(ConnectionJob job);

  /**
   * @brief Pop one job, waiting until work exists or shutdown is requested.
   * @return A job when available, otherwise `std::nullopt` after shutdown and
   *         full drain.
   */
  [[nodiscard]] std::optional<ConnectionJob> pop_blocking();

  /**
   * @brief Return current pending job count.
   * @return Number of jobs currently queued.
   */
  [[nodiscard]] std::size_t size() const noexcept;

  /**
   * @brief Request queue shutdown and wake blocked consumers.
   *
   * Existing jobs remain consumable. Idempotent.
   */
  void shutdown();

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<ConnectionJob> queue_;
  bool shutdown_requested_{false};
};

} // namespace mqtt

