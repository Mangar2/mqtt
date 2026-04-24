#pragma once

/**
 * @file job_scheduler.h
 * @brief Per-connection serialization scheduler for executor jobs.
 */

#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "executor/connection_job.h"
#include "executor/job_queue.h"

namespace mqtt {

/**
 * @brief Forward declaration of StructuredTracer.
 */
class StructuredTracer;

/**
 * @brief Serializes jobs per connection fd.
 *
 * At most one active job per fd is dispatched to JobQueue at a time.
 */
class JobScheduler {
public:
  /**
   * @brief Construct scheduler writing dispatch-ready jobs to `job_queue`.
   * @param job_queue Underlying work queue used by workers.
    * @param tracer Optional structured tracer for scheduler diagnostics.
   */
    explicit JobScheduler(JobQueue &job_queue,
                  StructuredTracer *tracer = nullptr) noexcept;

  /**
   * @brief Submit a job for scheduling.
   * @param job Job to schedule.
   */
  void submit(ConnectionJob job);

  /**
   * @brief Mark completion of the active job for one fd.
   * @param connection_fd Connection file descriptor.
   * @return Next queued job for this fd when pending, otherwise nullopt.
   */
  [[nodiscard]] std::optional<ConnectionJob> mark_done(int connection_fd);

private:
  /**
   * @brief Scheduling state for one connection descriptor.
   */
  struct ScheduleState {
    bool active{false};
    std::optional<JobType> active_type;
    std::deque<ConnectionJob> backlog;
  };

  JobQueue &job_queue_;
  StructuredTracer *tracer_;
  std::mutex mutex_;
  std::unordered_map<int, ScheduleState> states_;
};

} // namespace mqtt

