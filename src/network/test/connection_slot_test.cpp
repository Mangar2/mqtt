#include <catch2/catch_test_macros.hpp>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
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

} // namespace

TEST_CASE("slot_constructed_with_fd_starts_in_connecting_phase", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(42));
  CHECK(slot.fd() == static_cast<SocketHandle>(42));
  CHECK(slot.phase() == ConnectionPhase::Connecting);
}

TEST_CASE("read_buffer_push_pop_preserves_bytes", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(7), 16U, 16U);
  const std::array<uint8_t, 5> source{1, 2, 3, 4, 5};

  REQUIRE(slot.push_read_bytes(source));
  REQUIRE(slot.read_size() == source.size());
  REQUIRE(slot.read_contiguous_bytes().size() == source.size());
  CHECK(std::equal(slot.read_contiguous_bytes().begin(),
                   slot.read_contiguous_bytes().end(), source.begin()));

  REQUIRE(slot.pop_read_bytes(2U) == 2U);
  const std::array<uint8_t, 3> tail{3, 4, 5};
  CHECK(std::equal(slot.read_contiguous_bytes().begin(),
                   slot.read_contiguous_bytes().end(), tail.begin()));
}

TEST_CASE("read_buffer_full_rejects_push_returns_false", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(9), 4U, 8U);
  const std::array<uint8_t, 4> fill{0x10, 0x11, 0x12, 0x13};
  const std::array<uint8_t, 1> extra{0x14};
  REQUIRE(slot.push_read_bytes(fill));
  CHECK_FALSE(slot.push_read_bytes(extra));
}

TEST_CASE("write_buffer_drains_in_fifo_order", "[network]") {
  ConnectionSlot slot(static_cast<SocketHandle>(11), 8U, 4U);

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

TEST_CASE("move_constructor_transfers_fd_and_buffers", "[network]") {
  ConnectionSlot source_slot(static_cast<SocketHandle>(21), 8U, 8U);
  REQUIRE(source_slot.push_read_bytes(std::array<uint8_t, 3>{7, 8, 9}));
  REQUIRE(source_slot.push_write_bytes(std::array<uint8_t, 2>{4, 5}));

  ConnectionSlot moved_slot(std::move(source_slot));
  CHECK(source_slot.fd() == k_invalid_socket);
  CHECK(moved_slot.fd() == static_cast<SocketHandle>(21));

  const std::array<uint8_t, 3> expected_read{7, 8, 9};
  CHECK(std::equal(moved_slot.read_contiguous_bytes().begin(),
                   moved_slot.read_contiguous_bytes().end(),
                   expected_read.begin()));

  const std::array<uint8_t, 2> expected_write{4, 5};
  CHECK(std::equal(moved_slot.write_contiguous_bytes().begin(),
                   moved_slot.write_contiguous_bytes().end(),
                   expected_write.begin()));
}
