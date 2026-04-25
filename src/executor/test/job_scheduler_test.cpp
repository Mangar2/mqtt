#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "executor/job_scheduler.h"

using namespace mqtt;

namespace {

void update_max(std::atomic<int> &max_value, int current_value) {
  int observed_max = max_value.load(std::memory_order_acquire);
  while (current_value > observed_max &&
         !max_value.compare_exchange_weak(observed_max, current_value,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
  }
}

} // namespace

TEST_CASE("submit_for_idle_connection_enqueues_immediately", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  scheduler.submit(ConnectionJob{JobType::Decode, 11, DecodeJobPayload{}});

  const auto popped = queue.pop_blocking();
  REQUIRE(popped.has_value());
  CHECK(popped->connection_fd == 11);
}

TEST_CASE("submit_for_busy_connection_buffers_in_backlog", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  scheduler.submit(ConnectionJob{JobType::Decode, 77, DecodeJobPayload{1U}});
  scheduler.submit(ConnectionJob{JobType::Drain, 77, DrainJobPayload{2U}});

  const auto first = queue.pop_blocking();
  REQUIRE(first.has_value());
  CHECK(first->type == JobType::Decode);
  CHECK(queue.size() == 0U);
}

TEST_CASE("mark_done_returns_next_backlog_job_when_pending", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  scheduler.submit(ConnectionJob{JobType::Decode, 90, DecodeJobPayload{3U}});
  scheduler.submit(ConnectionJob{JobType::Drain, 90, DrainJobPayload{4U}});
  (void)queue.pop_blocking();

  const auto next = scheduler.mark_done(90);
  REQUIRE(next.has_value());
  CHECK(next->type == JobType::Drain);
}

TEST_CASE("mark_done_returns_nullopt_when_no_backlog", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  scheduler.submit(ConnectionJob{JobType::Decode, 33, DecodeJobPayload{}});
  (void)queue.pop_blocking();
  CHECK_FALSE(scheduler.mark_done(33).has_value());
}

TEST_CASE("mark_done_returns_nullopt_for_unknown_fd", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  CHECK_FALSE(scheduler.mark_done(9999).has_value());
}

TEST_CASE("at_most_one_active_job_per_fd_under_concurrent_submit", "[executor]") {
  JobQueue queue;
  JobScheduler scheduler(queue);

  constexpr int fd_count = 40;
  constexpr int jobs_per_fd = 200;
  constexpr int producer_thread_count = 8;
  constexpr int worker_thread_count = 6;

  std::vector<std::atomic<int>> active_per_fd(fd_count);
  std::vector<std::atomic<int>> max_active_per_fd(fd_count);
  for (int fd_index = 0; fd_index < fd_count; ++fd_index) {
    active_per_fd[static_cast<std::size_t>(fd_index)].store(
        0, std::memory_order_relaxed);
    max_active_per_fd[static_cast<std::size_t>(fd_index)].store(
        0, std::memory_order_relaxed);
  }

  std::atomic<int> processed_jobs{0};
  std::atomic<bool> producer_start{false};
  std::atomic<bool> producers_done{false};

  std::vector<std::thread> producer_threads;
  producer_threads.reserve(producer_thread_count);
  for (int producer_index = 0; producer_index < producer_thread_count;
       ++producer_index) {
    producer_threads.emplace_back([&scheduler, &producer_start, producer_index] {
      while (!producer_start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int fd_value = producer_index; fd_value < fd_count;
           fd_value += producer_thread_count) {
        for (int submit_index = 0; submit_index < jobs_per_fd; ++submit_index) {
          scheduler.submit(ConnectionJob{
              JobType::Decode, fd_value,
              DecodeJobPayload{.budget_bytes = static_cast<std::size_t>(submit_index)}});
        }
      }
    });
  }

  std::vector<std::thread> worker_threads;
  worker_threads.reserve(worker_thread_count);
  for (int worker_index = 0; worker_index < worker_thread_count; ++worker_index) {
    worker_threads.emplace_back([&queue, &scheduler, &active_per_fd,
                                 &max_active_per_fd, &processed_jobs] {
      while (true) {
        auto maybe_job = queue.pop_blocking();
        if (!maybe_job.has_value()) {
          return;
        }

        const int fd_value = maybe_job->connection_fd;
        auto &active = active_per_fd[static_cast<std::size_t>(fd_value)];
        auto &max_active = max_active_per_fd[static_cast<std::size_t>(fd_value)];

        const int now_active =
            active.fetch_add(1, std::memory_order_acq_rel) + 1;
        update_max(max_active, now_active);

        std::this_thread::sleep_for(std::chrono::microseconds(50));

        active.fetch_sub(1, std::memory_order_acq_rel);
        const auto deferred = scheduler.mark_done(fd_value);
        if (deferred.has_value()) {
          queue.push(std::move(*deferred));
        }

        (void)processed_jobs.fetch_add(1, std::memory_order_acq_rel);
      }
    });
  }

  producer_start.store(true, std::memory_order_release);

  for (std::thread &producer_thread : producer_threads) {
    producer_thread.join();
  }
  producers_done.store(true, std::memory_order_release);

  auto all_fds_idle = [&active_per_fd]() {
    for (std::atomic<int> &active_count : active_per_fd) {
      if (active_count.load(std::memory_order_acquire) != 0) {
        return false;
      }
    }
    return true;
  };

  const auto quiet_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < quiet_deadline) {
    if (producers_done.load(std::memory_order_acquire) && queue.size() == 0U &&
        all_fds_idle()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  queue.shutdown();

  for (std::thread &worker_thread : worker_threads) {
    worker_thread.join();
  }

  CHECK(processed_jobs.load(std::memory_order_acquire) > 0);
  for (int fd_index = 0; fd_index < fd_count; ++fd_index) {
    CHECK(max_active_per_fd[static_cast<std::size_t>(fd_index)].load(
              std::memory_order_acquire) <= 1);
  }
}

