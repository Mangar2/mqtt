#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/qos.h"
#include "store/inflight_store.h"
#include "store/store_error.h"

using namespace mqtt;

namespace {

InflightEntry
make_entry(uint16_t pkt_id, InflightDirection dir = InflightDirection::Outbound,
           InflightState state = InflightState::WaitingForPuback) {
  InflightEntry ent;
  ent.packet_id = pkt_id;
  ent.direction = dir;
  ent.state = state;
  ent.qos = QoS::AtLeastOnce;
  return ent;
}

std::vector<InflightEntry> collect_entries(InflightStore &store,
                                           std::string_view client_id) {
  std::vector<InflightEntry> entries;
  store.for_each(client_id,
                 [&entries](const InflightEntry &entry) { entries.push_back(entry); });
  return entries;
}

std::vector<InflightEntry>
collect_direction_entries(InflightStore &store, std::string_view client_id,
                          InflightDirection direction) {
  std::vector<InflightEntry> entries;
  store.for_each(client_id, direction, [&entries](const InflightEntry &entry) {
    entries.push_back(entry);
  });
  return entries;
}

} // namespace

TEST_CASE("create_entry_and_size", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U);
  store.create("c1", std::move(entry));
  CHECK(store.size_for("c1") == 1U);
}

TEST_CASE("create_multiple_entries", "[store]") {
  InflightStore store;
  InflightEntry first_entry = make_entry(1U);
  InflightEntry second_entry = make_entry(2U);
  store.create("c1", std::move(first_entry));
  store.create("c1", std::move(second_entry));
  CHECK(store.size_for("c1") == 2U);
}

TEST_CASE("for_each_returns_all", "[store]") {
  InflightStore store;
  InflightEntry first_entry = make_entry(1U);
  InflightEntry second_entry = make_entry(2U);
  store.create("c1", std::move(first_entry));
  store.create("c1", std::move(second_entry));
  const auto entries = collect_entries(store, "c1");
  CHECK(entries.size() == 2U);
}

TEST_CASE("for_each_unknown_client", "[store]") {
  InflightStore store;
  CHECK(collect_entries(store, "unknown").empty());
}

TEST_CASE("update_changes_state", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U, InflightDirection::Outbound,
                                   InflightState::WaitingForPuback);
  store.create("c1", std::move(entry));
  store.update("c1", 1U, InflightDirection::Outbound,
               InflightState::WaitingForPubrec);

  const auto entries = collect_entries(store, "c1");
  REQUIRE(entries.size() == 1U);
  CHECK(entries.front().state == InflightState::WaitingForPubrec);
}

TEST_CASE("update_unknown_throws", "[store]") {
  InflightStore store;
  CHECK_THROWS_AS(store.update("c1", 99U, InflightDirection::Outbound,
                               InflightState::WaitingForPubrec),
                  StoreException);
  try {
    store.update("c1", 99U, InflightDirection::Outbound,
                 InflightState::WaitingForPubrec);
  } catch (const StoreException &exc) {
    CHECK(exc.error() == StoreError::PacketIdNotFound);
  }
}

TEST_CASE("remove_entry_decrements_size", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U);
  store.create("c1", std::move(entry));
  store.remove("c1", 1U, InflightDirection::Outbound);
  CHECK(store.size_for("c1") == 0U);
}

TEST_CASE("inflight_remove_unknown_is_noop", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U);
  store.create("c1", std::move(entry));
  store.remove("c1", 99U, InflightDirection::Outbound);
  CHECK(store.size_for("c1") == 1U);
}

TEST_CASE("remove_last_entry_removes_session_bucket", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U);
  store.create("c1", std::move(entry));
  store.remove("c1", 1U, InflightDirection::Outbound);
  CHECK(collect_entries(store, "c1").empty());
}

TEST_CASE("is_packet_id_in_use_true", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U, InflightDirection::Outbound);
  store.create("c1", std::move(entry));
  CHECK(store.is_packet_id_in_use("c1", 1U, InflightDirection::Outbound));
}

TEST_CASE("is_packet_id_in_use_false", "[store]") {
  InflightStore store;
  CHECK_FALSE(
      store.is_packet_id_in_use("c1", 99U, InflightDirection::Outbound));
}

TEST_CASE("is_packet_id_in_use_direction_mismatch", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(1U, InflightDirection::Outbound);
  store.create("c1", std::move(entry));
  CHECK_FALSE(store.is_packet_id_in_use("c1", 1U, InflightDirection::Inbound));
}

TEST_CASE("for_each_does_not_include_other_clients", "[store]") {
  InflightStore store;
  InflightEntry first_entry = make_entry(1U);
  InflightEntry second_entry = make_entry(2U);
  store.create("c1", std::move(first_entry));
  store.create("c2", std::move(second_entry));
  const auto entries = collect_entries(store, "c1");
  REQUIRE(entries.size() == 1U);
  CHECK(entries.front().packet_id == 1U);
}

TEST_CASE("create_duplicate_packet_id_throws", "[store]") {
  InflightStore store;
  InflightEntry first_entry = make_entry(9U);
  InflightEntry duplicate_entry = make_entry(9U);
  store.create("c1", std::move(first_entry));

  CHECK_THROWS_AS(store.create("c1", std::move(duplicate_entry)), StoreException);
  try {
    store.create("c1", std::move(duplicate_entry));
  } catch (const StoreException &exception) {
    CHECK(exception.error() == StoreError::PacketIdAlreadyInUse);
  }
}

TEST_CASE("create_packet_id_zero_throws", "[store]") {
  InflightStore store;
  InflightEntry invalid_entry = make_entry(0U);

  CHECK_THROWS_AS(store.create("c1", std::move(invalid_entry)), StoreException);
  try {
    store.create("c1", std::move(invalid_entry));
  } catch (const StoreException &exception) {
    CHECK(exception.error() == StoreError::InvalidPacketId);
  }
}

TEST_CASE("drop_session_removes_all_entries", "[store]") {
  InflightStore store;
  InflightEntry outbound_entry = make_entry(1U, InflightDirection::Outbound);
  InflightEntry inbound_entry = make_entry(2U, InflightDirection::Inbound,
                                           InflightState::WaitingForPubrel);
  store.create("c1", std::move(outbound_entry));
  store.create("c1", std::move(inbound_entry));

  store.drop_session("c1");

  CHECK(store.size_for("c1") == 0U);
  CHECK(store.total_size() == 0U);
}

TEST_CASE("create_same_packet_id_in_both_directions_allowed", "[store]") {
  InflightStore store;
  InflightEntry outbound_entry = make_entry(11U, InflightDirection::Outbound);
  InflightEntry inbound_entry = make_entry(11U, InflightDirection::Inbound,
                                           InflightState::WaitingForPubrel);

  store.create("c1", std::move(outbound_entry));
  store.create("c1", std::move(inbound_entry));

  CHECK(store.size_for("c1") == 2U);
  CHECK(store.is_packet_id_in_use("c1", 11U, InflightDirection::Outbound));
  CHECK(store.is_packet_id_in_use("c1", 11U, InflightDirection::Inbound));
}

TEST_CASE("update_wrong_direction_throws_when_entry_exists_other_direction",
          "[store]") {
  InflightStore store;
  InflightEntry inbound_entry = make_entry(13U, InflightDirection::Inbound,
                                           InflightState::WaitingForPubrel);
  store.create("c1", std::move(inbound_entry));

  CHECK_THROWS_AS(store.update("c1", 13U, InflightDirection::Outbound,
                               InflightState::WaitingForPubcomp),
                  StoreException);
}

TEST_CASE("with_entry_unknown_returns_false_and_does_not_invoke_visitor",
          "[store]") {
  InflightStore store;
  bool visitor_called = false;

  const bool found = store.with_entry(
      "missing-client", 1U, InflightDirection::Outbound,
      [&visitor_called](const InflightEntry &) { visitor_called = true; });

  CHECK_FALSE(found);
  CHECK_FALSE(visitor_called);
}

TEST_CASE("with_entry_existing_returns_true_and_visits", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(21U, InflightDirection::Outbound,
                                   InflightState::WaitingForPuback);
  store.create("c1", std::move(entry));

  InflightState visited_state = InflightState::WaitingForPubrec;
  const bool found = store.with_entry(
      "c1", 21U, InflightDirection::Outbound,
      [&visited_state](const InflightEntry &stored_entry) {
        visited_state = stored_entry.state;
      });

  CHECK(found);
  CHECK(visited_state == InflightState::WaitingForPuback);
}

TEST_CASE("for_each_direction_filters_correctly", "[store]") {
  InflightStore store;
  InflightEntry outbound_entry = make_entry(31U, InflightDirection::Outbound);
  InflightEntry inbound_entry = make_entry(32U, InflightDirection::Inbound,
                                           InflightState::WaitingForPubrel);
  store.create("c1", std::move(outbound_entry));
  store.create("c1", std::move(inbound_entry));

  const auto outbound_entries =
      collect_direction_entries(store, "c1", InflightDirection::Outbound);
  const auto inbound_entries =
      collect_direction_entries(store, "c1", InflightDirection::Inbound);

  REQUIRE(outbound_entries.size() == 1U);
  CHECK(outbound_entries.front().packet_id == 31U);
  REQUIRE(inbound_entries.size() == 1U);
  CHECK(inbound_entries.front().packet_id == 32U);
}

TEST_CASE("snapshot_each_session_visits_all_clients_and_directions", "[store]") {
  InflightStore store;
  InflightEntry first_client_outbound =
      make_entry(41U, InflightDirection::Outbound);
  InflightEntry first_client_inbound =
      make_entry(42U, InflightDirection::Inbound, InflightState::WaitingForPubrel);
  InflightEntry second_client_outbound =
      make_entry(43U, InflightDirection::Outbound);

  store.create("c1", std::move(first_client_outbound));
  store.create("c1", std::move(first_client_inbound));
  store.create("c2", std::move(second_client_outbound));

  std::vector<std::string> snapshot_keys;
  store.snapshot_each_session(
      [&snapshot_keys](std::string_view client_id,
                       const InflightEntry &entry) {
        const std::string direction_label =
            entry.direction == InflightDirection::Outbound ? "out" : "in";
        snapshot_keys.push_back(std::string(client_id) + ":" +
                                std::to_string(entry.packet_id) + ":" +
                                direction_label);
      });

  CHECK(snapshot_keys.size() == 3U);
  CHECK(std::ranges::find(snapshot_keys, "c1:41:out") != snapshot_keys.end());
  CHECK(std::ranges::find(snapshot_keys, "c1:42:in") != snapshot_keys.end());
  CHECK(std::ranges::find(snapshot_keys, "c2:43:out") != snapshot_keys.end());
}

TEST_CASE("total_size_tracks_create_remove_and_drop", "[store]") {
  InflightStore store;
  InflightEntry first_entry = make_entry(51U, InflightDirection::Outbound);
  InflightEntry second_entry = make_entry(52U, InflightDirection::Outbound);
  InflightEntry third_entry = make_entry(53U, InflightDirection::Inbound,
                                         InflightState::WaitingForPubrel);

  store.create("c1", std::move(first_entry));
  store.create("c1", std::move(second_entry));
  store.create("c2", std::move(third_entry));
  CHECK(store.total_size() == 3U);

  store.remove("c1", 52U, InflightDirection::Outbound);
  CHECK(store.total_size() == 2U);

  store.drop_session("c2");
  CHECK(store.total_size() == 1U);
}

TEST_CASE("drop_session_unknown_is_noop", "[store]") {
  InflightStore store;
  InflightEntry entry = make_entry(61U, InflightDirection::Outbound);
  store.create("c1", std::move(entry));

  store.drop_session("unknown");

  CHECK(store.total_size() == 1U);
  CHECK(store.size_for("c1") == 1U);
}
