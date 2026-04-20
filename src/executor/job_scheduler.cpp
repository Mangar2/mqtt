#include "executor/job_scheduler.h"

#include <utility>

namespace mqtt {

JobScheduler::JobScheduler(JobQueue &job_queue) noexcept
    : job_queue_(job_queue) {}

void JobScheduler::submit(ConnectionJob job) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  ScheduleState &state = states_[job.connection_fd];

  if (!state.active) {
    state.active = true;
    job_queue_.push(std::move(job));
    return;
  }

  state.backlog.push_back(std::move(job));
}

std::optional<ConnectionJob> JobScheduler::mark_done(int connection_fd) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  const auto state_iter = states_.find(connection_fd);
  if (state_iter == states_.end()) {
    return std::nullopt;
  }

  ScheduleState &state = state_iter->second;
  if (state.backlog.empty()) {
    states_.erase(state_iter);
    return std::nullopt;
  }

  ConnectionJob next_job = std::move(state.backlog.front());
  state.backlog.pop_front();
  return next_job;
}

} // namespace mqtt

