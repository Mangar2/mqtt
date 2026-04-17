#include <catch2/catch_test_macros.hpp>

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

} // namespace

TEST_CASE("create_entry_and_size", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  CHECK(store.size_for("c1") == 1U);
}

TEST_CASE("create_multiple_entries", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  store.create("c1", make_entry(2U));
  CHECK(store.size_for("c1") == 2U);
}

TEST_CASE("entries_for_returns_all", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  store.create("c1", make_entry(2U));
  const auto entries = store.entries_for("c1");
  CHECK(entries.size() == 2U);
}

TEST_CASE("entries_for_unknown_client", "[store]") {
  InflightStore store;
  CHECK(store.entries_for("unknown").empty());
}

TEST_CASE("update_changes_state", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U, InflightDirection::Outbound,
                                InflightState::WaitingForPuback));
  store.update("c1", 1U, InflightDirection::Outbound,
               InflightState::WaitingForPubrec);

  const auto entries = store.entries_for("c1");
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
  store.create("c1", make_entry(1U));
  store.remove("c1", 1U, InflightDirection::Outbound);
  CHECK(store.size_for("c1") == 0U);
}

TEST_CASE("inflight_remove_unknown_is_noop", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  store.remove("c1", 99U, InflightDirection::Outbound);
  CHECK(store.size_for("c1") == 1U);
}

TEST_CASE("remove_last_entry_removes_session_bucket", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  store.remove("c1", 1U, InflightDirection::Outbound);
  CHECK(store.entries_for("c1").empty());
}

TEST_CASE("is_packet_id_in_use_true", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U, InflightDirection::Outbound));
  CHECK(store.is_packet_id_in_use("c1", 1U, InflightDirection::Outbound));
}

TEST_CASE("is_packet_id_in_use_false", "[store]") {
  InflightStore store;
  CHECK_FALSE(
      store.is_packet_id_in_use("c1", 99U, InflightDirection::Outbound));
}

TEST_CASE("is_packet_id_in_use_direction_mismatch", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U, InflightDirection::Outbound));
  CHECK_FALSE(store.is_packet_id_in_use("c1", 1U, InflightDirection::Inbound));
}

TEST_CASE("entries_for_does_not_include_other_clients", "[store]") {
  InflightStore store;
  store.create("c1", make_entry(1U));
  store.create("c2", make_entry(2U));
  const auto entries = store.entries_for("c1");
  REQUIRE(entries.size() == 1U);
  CHECK(entries.front().packet_id == 1U);
}
