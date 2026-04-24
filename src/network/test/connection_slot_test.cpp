#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "network/connection_slot.h"

using namespace mqtt;

namespace {

[[nodiscard]] std::vector<uint8_t>
drain_all_contiguous_write_bytes(ConnectionSlot &slot) {
  std::vector<uint8_t> drained_bytes;
  while (slot.write_size() > 0U) {
    const std::span<const uint8_t> contiguous = slot.write_contiguous_bytes();
    drained_bytes.insert(drained_bytes.end(), contiguous.begin(),
                         contiguous.end());
    const std::size_t removed_bytes = slot.pop_write_bytes(contiguous.size());
    REQUIRE(removed_bytes == contiguous.size());
  }
  return drained_bytes;
}

void move_assign(ConnectionSlot &destination_slot, ConnectionSlot &source_slot) {
  destination_slot = std::move(source_slot);
}

} // namespace

TEST_CASE("slot_constructed_with_fd_starts_in_connecting_phase", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(42));
  CHECK(slot.fd() == static_cast<SocketHandle>(42));
  CHECK(slot.phase() == ConnectionPhase::Connecting);
}

TEST_CASE("slot_capacity_and_free_space_accessors_report_current_values",
          "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(43), 5U);
  CHECK(slot.write_capacity() == 5U);
  CHECK(slot.write_free_space() == 5U);

  REQUIRE(slot.push_write_bytes(std::array<uint8_t, 3>{9, 8, 7}));
  CHECK(slot.write_free_space() == 2U);
}

TEST_CASE("zero_capacity_write_capacity_is_clamped", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(12), 0U);

  // Write limit is clamped to at least one byte even when configured as zero.
  CHECK(slot.push_write_bytes(std::array<uint8_t, 1>{2}));
  CHECK_FALSE(slot.push_write_bytes(std::array<uint8_t, 1>{3}));
  CHECK(slot.pop_write_bytes(3U) == 1U);
  CHECK(slot.write_contiguous_bytes().empty());
}

TEST_CASE("write_buffer_drains_in_fifo_order", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(11), 4U);

  const std::array<uint8_t, 4> first_write{1, 2, 3, 4};
  REQUIRE(slot.push_write_bytes(first_write));
  REQUIRE(slot.pop_write_bytes(2U) == 2U); // consume 1,2

  const std::array<uint8_t, 2> second_write{5, 6}; // wraps around
  REQUIRE(slot.push_write_bytes(second_write));

  const std::vector<uint8_t> drained = drain_all_contiguous_write_bytes(slot);
  const std::vector<uint8_t> expected{3, 4, 5, 6};
  CHECK(drained == expected);
}

TEST_CASE("phase_transitions_connecting_connected_closing_are_legal",
          "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(13));

  REQUIRE(slot.transition_to(ConnectionPhase::Connected));
  CHECK(slot.phase() == ConnectionPhase::Connected);

  REQUIRE(slot.transition_to(ConnectionPhase::Closing));
  CHECK(slot.phase() == ConnectionPhase::Closing);
}

TEST_CASE("phase_transition_back_to_connecting_is_rejected", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(15));
  REQUIRE(slot.transition_to(ConnectionPhase::Connected));
  CHECK_FALSE(slot.transition_to(ConnectionPhase::Connecting));
  CHECK(slot.phase() == ConnectionPhase::Connected);
}

TEST_CASE("phase_transition_to_same_phase_returns_true", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(16));
  CHECK(slot.transition_to(ConnectionPhase::Connecting));
  REQUIRE(slot.transition_to(ConnectionPhase::Connected));
  CHECK(slot.transition_to(ConnectionPhase::Connected));
}

TEST_CASE("phase_transition_from_closing_is_rejected", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(17));
  REQUIRE(slot.transition_to(ConnectionPhase::Closing));
  CHECK_FALSE(slot.transition_to(ConnectionPhase::Connected));
}

TEST_CASE("move_constructor_transfers_fd_and_buffers", "[network]") {
  ConnectionSlot source_slot(static_cast<SocketHandle>(21), 8U);
  REQUIRE(source_slot.push_write_bytes(std::array<uint8_t, 2>{4, 5}));

  ConnectionSlot moved_slot(std::move(source_slot));
  CHECK(source_slot.fd() == k_invalid_socket);
  CHECK(moved_slot.fd() == static_cast<SocketHandle>(21));

  const std::array<uint8_t, 2> expected_write{4, 5};
  CHECK(std::equal(moved_slot.write_contiguous_bytes().begin(),
                   moved_slot.write_contiguous_bytes().end(),
                   expected_write.begin()));
}

TEST_CASE("move_assignment_transfers_fd_and_buffers", "[network]") {
  ConnectionSlot source_slot(static_cast<SocketHandle>(31), 8U);
  REQUIRE(source_slot.push_write_bytes(std::array<uint8_t, 3>{0xB1, 0xB2, 0xB3}));
  REQUIRE(source_slot.transition_to(ConnectionPhase::Connected));

  ConnectionSlot target_slot(static_cast<SocketHandle>(32), 4U);
  target_slot = std::move(source_slot);

  CHECK(source_slot.fd() == k_invalid_socket);
  CHECK(source_slot.phase() == ConnectionPhase::Closing);
  CHECK(target_slot.fd() == static_cast<SocketHandle>(31));
  CHECK(target_slot.phase() == ConnectionPhase::Connected);

  const std::array<uint8_t, 3> expected_write{0xB1, 0xB2, 0xB3};
  CHECK(std::equal(target_slot.write_contiguous_bytes().begin(),
                   target_slot.write_contiguous_bytes().end(),
                   expected_write.begin()));
}

TEST_CASE("move_assignment_to_self_is_noop", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(33), 8U);
  REQUIRE(slot.push_write_bytes(std::array<uint8_t, 2>{6, 7}));
  REQUIRE(slot.transition_to(ConnectionPhase::Connected));

  move_assign(slot, slot);

  CHECK(slot.fd() == static_cast<SocketHandle>(33));
  CHECK(slot.phase() == ConnectionPhase::Connected);
  const std::array<uint8_t, 2> expected_write{6, 7};
  CHECK(std::equal(slot.write_contiguous_bytes().begin(),
                   slot.write_contiguous_bytes().end(),
                   expected_write.begin()));
}
