#include "executor/job_scheduler.h"

#include <string_view>
#include <utility>

#include "monitoring/structured_tracer.h"

namespace mqtt {

namespace {

[[maybe_unused]] std::string_view job_type_name(JobType job_type) {
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

  TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "executor";
    event.info = "scheduler_submit_active";
    event.data.emplace_back("connection_fd", std::to_string(job.connection_fd));
    event.data.emplace_back("job_type", std::string(job_type_name(job.type)));
    event.data.emplace_back("active_type",
        std::string(job_type_name(state.active_type.value_or(JobType::Accept))));
    event.data.emplace_back("backlog_size", std::to_string(state.backlog.size()));
    tracer_->emit(event);
  }

  if (is_suspicious_backlog_type(job.type)) {
    const int connection_fd = job.connection_fd;
    const JobType submitted_type = job.type;
    const bool had_pending = job.type == JobType::Decode
                                 ? state.pending_decode_job.has_value()
                                 : state.pending_drain_job.has_value();

    if (job.type == JobType::Decode) {
      state.pending_decode_job = std::move(job);
    } else {
      state.pending_drain_job = std::move(job);
    }

    if (had_pending) {
      TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
        TraceEvent event;
        event.level = TraceLevel::Trace;
        event.module = "executor";
        event.info = "scheduler_job_dropped";
        event.data.emplace_back("connection_fd", std::to_string(connection_fd));
        event.data.emplace_back("job_type", std::string(job_type_name(submitted_type)));
        event.data.emplace_back("active_type",
            std::string(job_type_name(state.active_type.value_or(JobType::Accept))));
        event.data.emplace_back("backlog_size", std::to_string(state.backlog.size()));
        event.data.emplace_back("coalesced", "true");
        tracer_->emit(event);
      }
    }
#ifndef MQTT_TRACING_DISABLED
    TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "executor";
      event.info = "scheduler_backlog_enqueue";
        event.data.emplace_back("connection_fd", std::to_string(connection_fd));
        event.data.emplace_back("job_type", std::string(job_type_name(submitted_type)));
      event.data.emplace_back(
          "active_type",
          std::string(job_type_name(state.active_type.value_or(JobType::Accept))));
      event.data.emplace_back("backlog_size_before", std::to_string(state.backlog.size()));
      event.data.emplace_back("pending_decode", state.pending_decode_job.has_value() ? "true" : "false");
      event.data.emplace_back("pending_drain", state.pending_drain_job.has_value() ? "true" : "false");
      tracer_->emit(event);
    }
#endif
    return;
  }

  state.backlog.push_back(std::move(job));
}

std::optional<ConnectionJob> JobScheduler::mark_done(int connection_fd) {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  const auto state_iter = states_.find(connection_fd);
  if (state_iter == states_.end()) {
    TRACE_GUARD(tracer_, TraceLevel::Info, "executor") {
      TraceEvent event;
      event.level = TraceLevel::Info;
      event.module = "executor";
      event.info = "mark_done_unknown_fd";
      event.data.emplace_back("connection_fd", std::to_string(connection_fd));
      tracer_->emit(event);
    }
    return std::nullopt;
  }

  ScheduleState &state = state_iter->second;
  TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
    TraceEvent event;
    event.level = TraceLevel::Trace;
    event.module = "executor";
    event.info = "mark_done";
    event.data.emplace_back("connection_fd", std::to_string(connection_fd));
    event.data.emplace_back("backlog_size", std::to_string(state.backlog.size()));
    event.data.emplace_back("pending_decode", state.pending_decode_job.has_value() ? "true" : "false");
    event.data.emplace_back("pending_drain", state.pending_drain_job.has_value() ? "true" : "false");
    tracer_->emit(event);
  }

  if (state.pending_decode_job.has_value() && state.pending_drain_job.has_value()) {
    if (state.prefer_drain_next) {
      std::optional<ConnectionJob> pending_job = std::move(state.pending_drain_job);
      state.pending_drain_job.reset();
      state.prefer_drain_next = false;
      state.active_type = pending_job->type;
      return pending_job;
    }
    std::optional<ConnectionJob> pending_job = std::move(state.pending_decode_job);
    state.pending_decode_job.reset();
    state.prefer_drain_next = true;
    state.active_type = pending_job->type;
    return pending_job;
  }

  if (state.pending_drain_job.has_value()) {
    std::optional<ConnectionJob> pending_job = std::move(state.pending_drain_job);
    state.pending_drain_job.reset();
    state.prefer_drain_next = false;
    state.active_type = pending_job->type;
    return pending_job;
  }

  if (state.pending_decode_job.has_value()) {
    std::optional<ConnectionJob> pending_job = std::move(state.pending_decode_job);
    state.pending_decode_job.reset();
    state.prefer_drain_next = true;
    state.active_type = pending_job->type;
    return pending_job;
  }

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

