#include "executor/worker_pool.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "executor/pool_scaling_policy.h"
#include "monitoring/structured_tracer.h"

namespace mqtt {

namespace {

class BusyCounterGuard {
public:
  explicit BusyCounterGuard(std::atomic<std::size_t> &busy_counter) noexcept
      : busy_counter_(busy_counter) {
    busy_counter_.fetch_add(1U, std::memory_order_relaxed);
  }

  ~BusyCounterGuard() {
    busy_counter_.fetch_sub(1U, std::memory_order_relaxed);
  }

private:
  std::atomic<std::size_t> &busy_counter_;
};

} // namespace

WorkerPool::WorkerPool(JobHandler job_handler, std::size_t max_threads,
                       StructuredTracer *tracer)
    : job_handler_(std::move(job_handler)),
      max_threads_(max_threads == 0U ? compute_default_max_threads()
                                     : max_threads),
      job_scheduler_(job_queue_, tracer),
      tracer_(tracer) {
  if (!job_handler_) {
    throw std::invalid_argument("WorkerPool requires a valid job handler");
  }
  if (max_threads_ == 0U) {
    throw std::invalid_argument("WorkerPool max_threads must be > 0");
  }
}

WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::start(std::size_t min_threads) {
  if (min_threads == 0U) {
    throw std::invalid_argument("WorkerPool min_threads must be > 0");
  }

  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if (running_.load(std::memory_order_acquire)) {
      return;
    }
    running_.store(true, std::memory_order_release);
    stop_requested_.store(false, std::memory_order_release);
  }

  const std::size_t startup_threads = std::min(min_threads, max_threads_);
  for (std::size_t thread_index = 0; thread_index < startup_threads;
       ++thread_index) {
    spawn_worker();
  }

  std::lock_guard<std::mutex> lock_guard(mutex_);
  scaling_thread_ = std::thread(&WorkerPool::scaling_loop, this);
}

void WorkerPool::stop() {
  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    running_.store(false, std::memory_order_release);
    stop_requested_.store(true, std::memory_order_release);
  }

  job_queue_.shutdown();

  std::thread scaling_thread;
  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    scaling_thread = std::move(scaling_thread_);
  }
  if (scaling_thread.joinable()) {
    scaling_thread.join();
  }

  std::vector<std::thread> worker_threads;
  {
    std::lock_guard<std::mutex> lock_guard(mutex_);
    worker_threads = std::move(worker_threads_);
  }

  for (std::thread &worker_thread : worker_threads) {
    if (worker_thread.joinable()) {
      worker_thread.join();
    }
  }
}

void WorkerPool::submit(ConnectionJob job) {
  if (!running_.load(std::memory_order_acquire)) {
    throw std::logic_error("WorkerPool submit() called while pool is stopped");
  }
  job_scheduler_.submit(std::move(job));
}

JobScheduler &WorkerPool::job_scheduler() noexcept { return job_scheduler_; }

std::size_t WorkerPool::worker_count() const noexcept {
  return worker_count_.load(std::memory_order_acquire);
}

std::size_t WorkerPool::busy_count() const noexcept {
  return busy_count_.load(std::memory_order_acquire);
}

bool WorkerPool::is_running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

std::size_t WorkerPool::compute_default_max_threads() noexcept {
  const std::size_t hardware_threads =
      std::max(2U, std::thread::hardware_concurrency());
  return hardware_threads * 4U;
}

void WorkerPool::worker_loop() {
  while (true) {
    std::optional<ConnectionJob> job = job_queue_.pop_blocking();
    if (!job.has_value()) {
      break;
    }

    BusyCounterGuard busy_guard(busy_count_);
    TRACE_GUARD(tracer_, TraceLevel::Trace, "executor") {
      TraceEvent event;
      event.level = TraceLevel::Trace;
      event.module = "executor";
      event.info = "worker_job_pop";
      event.data.emplace_back("connection_fd", std::to_string(job->connection_fd));
      event.data.emplace_back("job_type",
          job->type == JobType::Decode ? "Decode" :
          job->type == JobType::Drain  ? "Drain"  :
          job->type == JobType::Close  ? "Close"  : "Accept");
      tracer_->emit(event);
    }
    job_handler_(*job);

    std::optional<ConnectionJob> deferred = job_scheduler_.mark_done(job->connection_fd);
    if (deferred.has_value()) {
      job_queue_.push(std::move(*deferred));
    }
  }

  worker_count_.fetch_sub(1U, std::memory_order_relaxed);
}

void WorkerPool::scaling_loop() {
  using namespace std::chrono_literals;

  double queue_depth_avg = 0.0;
  while (!stop_requested_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(250ms);
    if (stop_requested_.load(std::memory_order_acquire)) {
      break;
    }

    const double queue_depth_sample =
        static_cast<double>(job_queue_.size());
    queue_depth_avg = (queue_depth_avg * 3.0 + queue_depth_sample) / 4.0;

    const std::size_t active_workers =
        worker_count_.load(std::memory_order_acquire);
    if (active_workers == 0U) {
      continue;
    }

    const double busy_ratio =
        static_cast<double>(busy_count_.load(std::memory_order_acquire)) /
        static_cast<double>(active_workers);

    if (should_grow(queue_depth_avg, active_workers, busy_ratio, max_threads_)) {
      spawn_worker();
    }
  }
}

void WorkerPool::spawn_worker() {
  std::lock_guard<std::mutex> lock_guard(mutex_);
  if (!running_.load(std::memory_order_acquire)) {
    return;
  }
  if (worker_threads_.size() >= max_threads_) {
    return;
  }
  worker_threads_.emplace_back(&WorkerPool::worker_loop, this);
  worker_count_.fetch_add(1U, std::memory_order_relaxed);
}

} // namespace mqtt

