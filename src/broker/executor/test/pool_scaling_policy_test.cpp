#include <catch2/catch_test_macros.hpp>

#include "executor/pool_scaling_policy.h"

using namespace mqtt;

TEST_CASE("does_not_grow_when_queue_empty", "[executor]") {
  CHECK_FALSE(should_grow(0.0, 4U, 0.99, 16U));
}

TEST_CASE("does_not_grow_when_busy_ratio_below_threshold", "[executor]") {
  CHECK_FALSE(should_grow(20.0, 4U, 0.50, 16U));
}

TEST_CASE("grows_when_queue_deep_and_workers_busy", "[executor]") {
  CHECK(should_grow(9.0, 4U, 0.90, 16U));
}

TEST_CASE("does_not_grow_above_max_threads", "[executor]") {
  CHECK_FALSE(should_grow(100.0, 8U, 0.95, 8U));
}

