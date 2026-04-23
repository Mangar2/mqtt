#include "executor/job_scheduler.h"

#include <string_view>
#include <utility>

#include "monitoring/structured_tracer.h"

namespace mqtt {

namespace {

std::string_view job_type_name(JobType job_type) {
  switch (job_type) {
  case JobType::Accept:
    return "Accept";
  case JobType::Decode:
    return "Decode";
  case JobType::Drain:
    return "Drain";
  case JobType::Close:
    return "Close";
  }
  return "Unknown";
}

bool is_suspicious_backlog_type(JobType job_type) {
  return job_type == JobType::Decode || job_type == JobType::Drain;
}

bool backlog_contains_type(const std::deque<ConnectionJob> &backlog,
                          JobType job_type) {
  for (const ConnectionJob &queued_job : backlog) {
    if (queued_job.type == job_type) {
      return true;
    }
  }
  return false;
}

} // namespace

JobScheduler::JobScheduler(JobQueue &job_queue,
                           StructuredTracer *tracer) noexcept
    : job_queue_(job_queue), tracer_(tracer) {}

void JobScheduler::submit(ConnectionJob job) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  ScheduleState &state = states_[job.connection_fd];

  if (!state.active) {
    state.active = true;
    state.active_type = job.type;
    job_queue_.push(std::move(job));
    return;
  }

  if (is_suspicious_backlog_type(job.type)) {
    if (state.active_type.has_value() && state.active_type.value() == job.type) {
      return;
    }
    if (backlog_contains_type(state.backlog, job.type)) {
      return;
    }
  }

  if (is_suspicious_backlog_type(job.type)) {
    TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "executor";
      event.info = "scheduler_backlog_enqueue";
      event.data.emplace_back("connection_fd", std::to_string(job.connection_fd));
      event.data.emplace_back("job_type", std::string(job_type_name(job.type)));
      event.data.emplace_back(
          "active_type",
          std::string(job_type_name(state.active_type.value_or(JobType::Accept))));
      event.data.emplace_back("backlog_size_before",
                              std::to_string(state.backlog.size()));
      tracer_->emit(event);
    }
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
    state.active_type.reset();
    states_.erase(state_iter);
    return std::nullopt;
  }

  ConnectionJob next_job = std::move(state.backlog.front());
  state.backlog.pop_front();
  state.active_type = next_job.type;
  return next_job;
}

} // namespace mqtt

