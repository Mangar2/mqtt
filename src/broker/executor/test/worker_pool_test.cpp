#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "executor/worker_pool.h"

using namespace mqtt;

namespace {

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

} // namespace

TEST_CASE("start_creates_min_threads", "[executor]") {
  WorkerPool pool([](const ConnectionJob &) {}, 8U);
  pool.start(3U);

  REQUIRE(wait_until([&pool] { return pool.worker_count() == 3U; },
                     std::chrono::milliseconds(300)));
  pool.stop();
}

TEST_CASE("submit_executes_job_on_worker", "[executor]") {
  std::promise<void> executed_promise;
  auto executed_future = executed_promise.get_future();

  WorkerPool pool(
      [&executed_promise](const ConnectionJob &) { executed_promise.set_value(); },
      4U);
  pool.start(1U);
  pool.submit(ConnectionJob{JobType::Decode, 1, DecodeJobPayload{}});

  REQUIRE(executed_future.wait_for(std::chrono::seconds(1)) ==
          std::future_status::ready);
  pool.stop();
}

TEST_CASE("stop_drains_pending_and_joins_all_workers", "[executor]") {
  std::atomic<int> handled_jobs{0};

  WorkerPool pool([&handled_jobs](const ConnectionJob &) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    handled_jobs.fetch_add(1, std::memory_order_relaxed);
  });

  constexpr int job_count = 200;
  pool.start(2U);
  for (int index = 0; index < job_count; ++index) {
    pool.submit(ConnectionJob{JobType::Decode, index, DecodeJobPayload{}});
  }
  pool.stop();

  CHECK(handled_jobs.load(std::memory_order_acquire) == job_count);
  CHECK(pool.worker_count() == 0U);
}

TEST_CASE("pool_grows_under_sustained_load", "[executor]") {
  WorkerPool pool(
      [](const ConnectionJob &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      },
      4U);

  pool.start(1U);
  for (int index = 0; index < 900; ++index) {
    pool.submit(ConnectionJob{JobType::Decode, index, DecodeJobPayload{}});
  }

  REQUIRE(wait_until([&pool] { return pool.worker_count() > 1U; },
                     std::chrono::seconds(4)));
  CHECK(pool.worker_count() <= 4U);
  pool.stop();
}

TEST_CASE("pool_does_not_shrink_during_lifetime", "[executor]") {
  WorkerPool pool(
      [](const ConnectionJob &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      },
      4U);

  pool.start(1U);
  for (int index = 0; index < 900; ++index) {
    pool.submit(ConnectionJob{JobType::Decode, index, DecodeJobPayload{}});
  }

  REQUIRE(wait_until([&pool] { return pool.worker_count() > 1U; },
                     std::chrono::seconds(4)));
  const std::size_t grown_count = pool.worker_count();

  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  CHECK(pool.worker_count() >= grown_count);
  pool.stop();
}

TEST_CASE("no_thread_leak_after_stop", "[executor]") {
  WorkerPool pool(
      [](const ConnectionJob &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      },
      4U);

  pool.start(2U);
  for (int index = 0; index < 40; ++index) {
    pool.submit(ConnectionJob{JobType::Decode, index, DecodeJobPayload{}});
  }
  pool.stop();

  CHECK_FALSE(pool.is_running());
  CHECK(pool.worker_count() == 0U);
  CHECK(pool.busy_count() == 0U);
}

