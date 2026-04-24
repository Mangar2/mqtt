#pragma once

/**
 * @file worker_pool.h
 * @brief Elastic worker pool for connection jobs.
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "executor/connection_job.h"
#include "executor/job_queue.h"
#include "executor/job_scheduler.h"

namespace mqtt {

/**
 * @brief Forward declaration of StructuredTracer.
 */
class StructuredTracer;

/**
 * @brief Elastic worker pool with per-connection serialized scheduling.
 */
class WorkerPool {
public:
  /**
   * @brief Worker job callback type.
   */
  using JobHandler = std::function<void(const ConnectionJob &)>;

  /**
   * @brief Construct a worker pool.
   * @param job_handler Callable executed for each dequeued job.
   * @param max_threads Hard upper bound for worker thread count.
    * @param tracer Optional structured tracer for scheduler diagnostics.
   */
  explicit WorkerPool(JobHandler job_handler,
                 std::size_t max_threads = 0U,
                 StructuredTracer *tracer = nullptr);

  /**
   * @brief Destroy worker pool and join all threads.
   */
  ~WorkerPool();

  /**
   * @brief Start the pool with at least `min_threads` workers.
   * @param min_threads Initial worker count.
   */
  void start(std::size_t min_threads);

  /**
   * @brief Stop scaling and workers, and join all threads.
   */
  void stop();

  /**
   * @brief Submit a new connection job.
   * @param job Job to schedule.
   */
  void submit(ConnectionJob job);

  /**
   * @brief Access per-connection scheduler used by this pool.
   * @return Scheduler reference.
   */
  [[nodiscard]] JobScheduler &job_scheduler() noexcept;

  /**
   * @brief Return number of currently running worker threads.
   * @return Worker count.
   */
  [[nodiscard]] std::size_t worker_count() const noexcept;

  /**
   * @brief Return number of workers currently executing handlers.
   * @return Busy worker count.
   */
  [[nodiscard]] std::size_t busy_count() const noexcept;

  /**
   * @brief Return whether the pool has been started and not stopped.
   * @return `true` when running.
   */
  [[nodiscard]] bool is_running() const noexcept;

private:
  /**
   * @brief Compute default maximum worker count.
   * @return Default maximum thread count.
   */
  [[nodiscard]] static std::size_t compute_default_max_threads() noexcept;
  /**
   * @brief Worker thread loop.
   */
  void worker_loop();
  /**
   * @brief Scaling controller thread loop.
   */
  void scaling_loop();
  /**
   * @brief Spawn one worker thread.
   */
  void spawn_worker();

  JobHandler job_handler_;
  const std::size_t max_threads_;
  JobQueue job_queue_;
  JobScheduler job_scheduler_;

  mutable std::mutex mutex_;
  std::vector<std::thread> worker_threads_;
  std::thread scaling_thread_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::size_t> worker_count_{0U};
  std::atomic<std::size_t> busy_count_{0U};
};

} // namespace mqtt

