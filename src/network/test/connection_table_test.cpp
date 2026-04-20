#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "network/connection_table.h"

using namespace mqtt;

TEST_CASE("add_then_find_returns_slot_pointer", "[network]") {
  ConnectionTable table;
  REQUIRE(table.add(ConnectionSlot(static_cast<SocketHandle>(101))));
  ConnectionSlot *found_slot = table.find(101);
  REQUIRE(found_slot != nullptr);
  CHECK(found_slot->fd() == static_cast<SocketHandle>(101));
}

TEST_CASE("find_unknown_fd_returns_nullptr", "[network]") {
  ConnectionTable table;
  CHECK(table.find(404) == nullptr);
}

TEST_CASE("remove_unregisters_and_destroys_slot", "[network]") {
  ConnectionTable table;
  REQUIRE(table.add(ConnectionSlot(static_cast<SocketHandle>(202))));
  REQUIRE(table.remove(202));
  CHECK(table.find(202) == nullptr);
}

TEST_CASE("concurrent_find_from_many_threads_is_safe", "[network]") {
  ConnectionTable table;
  REQUIRE(table.add(ConnectionSlot(static_cast<SocketHandle>(303))));

  constexpr int worker_thread_count = 8;
  constexpr int lookup_iterations_per_thread = 10000;
  std::atomic<int> successful_lookups{0};
  std::vector<std::thread> worker_threads;
  worker_threads.reserve(static_cast<std::size_t>(worker_thread_count));

  for (int thread_index = 0; thread_index < worker_thread_count; ++thread_index) {
    worker_threads.emplace_back([&table, &successful_lookups] {
      for (int iteration_index = 0;
           iteration_index < lookup_iterations_per_thread; ++iteration_index) {
        if (table.find(303) != nullptr) {
          successful_lookups.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (std::thread &worker_thread : worker_threads) {
    worker_thread.join();
  }

  CHECK(successful_lookups.load(std::memory_order_relaxed) ==
        worker_thread_count * lookup_iterations_per_thread);
}

TEST_CASE("concurrent_add_remove_is_safe", "[network]") {
  ConnectionTable table;

  constexpr int connection_count = 1000;
  constexpr int add_thread_count = 4;
  constexpr int remove_thread_count = 4;
  std::vector<int> connection_fds;
  connection_fds.reserve(connection_count);
  for (int index = 0; index < connection_count; ++index) {
    connection_fds.push_back(10000 + index);
  }

  std::atomic<bool> start_signal{false};
  std::atomic<int> finished_add_threads{0};

  std::vector<std::thread> add_threads;
  add_threads.reserve(add_thread_count);
  for (int add_thread_index = 0; add_thread_index < add_thread_count;
       ++add_thread_index) {
    add_threads.emplace_back([&table, &connection_fds, &start_signal,
                              &finished_add_threads, add_thread_index] {
      while (!start_signal.load(std::memory_order_acquire)) {
      }

      const int begin_index = add_thread_index * (connection_count / add_thread_count);
      const int end_index = (add_thread_index + 1) * (connection_count / add_thread_count);
      for (int index = begin_index; index < end_index; ++index) {
        const bool inserted =
            table.add(ConnectionSlot(static_cast<SocketHandle>(connection_fds[index])));
        (void)inserted;
      }
      finished_add_threads.fetch_add(1, std::memory_order_release);
    });
  }

  std::vector<std::thread> remove_threads;
  remove_threads.reserve(remove_thread_count);
  for (int remove_thread_index = 0; remove_thread_index < remove_thread_count;
       ++remove_thread_index) {
    remove_threads.emplace_back([&table, &connection_fds, &start_signal,
                                 &finished_add_threads, remove_thread_index] {
      while (!start_signal.load(std::memory_order_acquire)) {
      }

      const int begin_index =
          remove_thread_index * (connection_count / remove_thread_count);
      const int end_index =
          (remove_thread_index + 1) * (connection_count / remove_thread_count);

      while (finished_add_threads.load(std::memory_order_acquire) <
             add_thread_count) {
        for (int index = begin_index; index < end_index; ++index) {
          const bool removed = table.remove(connection_fds[index]);
          (void)removed;
        }
      }

      for (int index = begin_index; index < end_index; ++index) {
        const bool removed = table.remove(connection_fds[index]);
        (void)removed;
      }
    });
  }

  start_signal.store(true, std::memory_order_release);

  for (std::thread &add_thread : add_threads) {
    add_thread.join();
  }
  for (std::thread &remove_thread : remove_threads) {
    remove_thread.join();
  }

  CHECK(table.size() == 0U);
  for (int connection_fd : connection_fds) {
    CHECK(table.find(connection_fd) == nullptr);
  }
}
