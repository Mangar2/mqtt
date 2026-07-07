#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include "executor/job_queue.h"

using namespace mqtt;

TEST_CASE("push_then_pop_returns_same_job", "[executor]") {
  JobQueue queue;
  queue.push(ConnectionJob{JobType::Decode, 100, DecodeJobPayload{1U}});

  const auto popped = queue.pop_blocking();
  REQUIRE(popped.has_value());
  CHECK(popped->type == JobType::Decode);
  CHECK(popped->connection_fd == 100);
  REQUIRE(std::holds_alternative<DecodeJobPayload>(popped->payload));
  CHECK(std::get<DecodeJobPayload>(popped->payload).budget_bytes == 1U);
}

TEST_CASE("pop_blocks_until_push", "[executor]") {
  JobQueue queue;
  std::atomic<bool> waiting_in_pop{false};
  std::optional<ConnectionJob> popped_job;

  std::thread consumer([&queue, &waiting_in_pop, &popped_job] {
    waiting_in_pop.store(true, std::memory_order_release);
    popped_job = queue.pop_blocking();
  });

  while (!waiting_in_pop.load(std::memory_order_acquire)) {
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  queue.push(ConnectionJob{JobType::Accept, 77, AcceptJobPayload{}});

  consumer.join();
  REQUIRE(popped_job.has_value());
  CHECK(popped_job->connection_fd == 77);
}

TEST_CASE("pop_returns_nullopt_after_shutdown", "[executor]") {
  JobQueue queue;
  queue.shutdown();
  const auto popped = queue.pop_blocking();
  CHECK_FALSE(popped.has_value());
}

TEST_CASE("fifo_order_preserved_under_concurrent_push", "[executor]") {
  JobQueue queue;

  constexpr int producer_count = 4;
  constexpr int jobs_per_producer = 500;
  constexpr int total_jobs = producer_count * jobs_per_producer;

  std::atomic<bool> start_signal{false};
  std::vector<std::thread> producer_threads;
  producer_threads.reserve(producer_count);

  for (int producer_index = 0; producer_index < producer_count;
       ++producer_index) {
    producer_threads.emplace_back([&queue, &start_signal, producer_index] {
      while (!start_signal.load(std::memory_order_acquire)) {
      }
      for (int sequence_index = 0; sequence_index < jobs_per_producer;
           ++sequence_index) {
        queue.push(ConnectionJob{
            JobType::Decode, producer_index,
            DecodeJobPayload{.budget_bytes = static_cast<std::size_t>(sequence_index)}});
      }
    });
  }

  start_signal.store(true, std::memory_order_release);

  std::map<int, std::vector<int>> observed_sequences_by_fd;
  for (int popped_count = 0; popped_count < total_jobs; ++popped_count) {
    const auto popped = queue.pop_blocking();
    REQUIRE(popped.has_value());
    REQUIRE(std::holds_alternative<DecodeJobPayload>(popped->payload));
    const auto seq = static_cast<int>(
        std::get<DecodeJobPayload>(popped->payload).budget_bytes);
    observed_sequences_by_fd[popped->connection_fd].push_back(seq);
  }

  for (std::thread &producer_thread : producer_threads) {
    producer_thread.join();
  }

  for (int producer_index = 0; producer_index < producer_count;
       ++producer_index) {
    const auto iter = observed_sequences_by_fd.find(producer_index);
    REQUIRE(iter != observed_sequences_by_fd.end());
    REQUIRE(static_cast<int>(iter->second.size()) == jobs_per_producer);
    for (int sequence_index = 0; sequence_index < jobs_per_producer;
         ++sequence_index) {
      CHECK(iter->second[static_cast<std::size_t>(sequence_index)] ==
            sequence_index);
    }
  }
}

TEST_CASE("size_reflects_pending_jobs", "[executor]") {
  JobQueue queue;
  CHECK(queue.size() == 0U);

  queue.push(ConnectionJob{JobType::Decode, 1, DecodeJobPayload{}});
  queue.push(ConnectionJob{JobType::Drain, 2, DrainJobPayload{}});
  CHECK(queue.size() == 2U);

  (void)queue.pop_blocking();
  CHECK(queue.size() == 1U);
}

