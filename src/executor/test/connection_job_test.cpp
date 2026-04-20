#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "executor/connection_job.h"

using namespace mqtt;

TEST_CASE("job_constructed_with_type_and_fd_holds_values", "[executor]") {
  ConnectionJob job{JobType::Drain, 42, DrainJobPayload{.budget_bytes = 128U}};

  CHECK(job.type == JobType::Drain);
  CHECK(job.connection_fd == 42);
  REQUIRE(std::holds_alternative<DrainJobPayload>(job.payload));
  CHECK(std::get<DrainJobPayload>(job.payload).budget_bytes == 128U);
}

TEST_CASE("job_payload_variant_round_trips_for_each_type", "[executor]") {
  const ConnectionJob accept_job{
      JobType::Accept, 10, AcceptJobPayload{.websocket_connection = true}};
  REQUIRE(std::holds_alternative<AcceptJobPayload>(accept_job.payload));
  CHECK(std::get<AcceptJobPayload>(accept_job.payload).websocket_connection);

  const ConnectionJob decode_job{JobType::Decode, 11,
                                 DecodeJobPayload{.budget_bytes = 64U}};
  REQUIRE(std::holds_alternative<DecodeJobPayload>(decode_job.payload));
  CHECK(std::get<DecodeJobPayload>(decode_job.payload).budget_bytes == 64U);

  const ConnectionJob drain_job{JobType::Drain, 12,
                                DrainJobPayload{.budget_bytes = 96U}};
  REQUIRE(std::holds_alternative<DrainJobPayload>(drain_job.payload));
  CHECK(std::get<DrainJobPayload>(drain_job.payload).budget_bytes == 96U);

  const ConnectionJob close_job{JobType::Close, 13,
                                CloseJobPayload{.immediate = true}};
  REQUIRE(std::holds_alternative<CloseJobPayload>(close_job.payload));
  CHECK(std::get<CloseJobPayload>(close_job.payload).immediate);
}

