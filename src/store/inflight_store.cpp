#include "store/inflight_store.h"

#include <algorithm>
#include <format>

#include "store/store_error.h"

namespace mqtt {

// ──────────────────────────────────────────────────────────────────────────────
// Private helper

/*static*/
std::vector<InflightEntry>::iterator
InflightStore::find_entry(std::vector<InflightEntry> &list, uint16_t packet_id,
                          InflightDirection direction) noexcept {
  return std::find_if(list.begin(), list.end(),
                      [packet_id, direction](const InflightEntry &ent) {
                        return ent.packet_id == packet_id &&
                               ent.direction == direction;
                      });
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API

void InflightStore::create(std::string_view client_id,
                           const InflightEntry &entry) {
  entries_[std::string(client_id)].push_back(entry);
}

void InflightStore::update(std::string_view client_id, uint16_t packet_id,
                           InflightDirection direction,
                           InflightState new_state) {
  const auto map_iter = entries_.find(std::string(client_id));
  if (map_iter != entries_.end()) {
    auto entry_iter = find_entry(map_iter->second, packet_id, direction);
    if (entry_iter != map_iter->second.end()) {
      entry_iter->state = new_state;
      return;
    }
  }
  throw StoreException(
      StoreError::PacketIdNotFound,
      std::format("inflight entry not found: client={}, packet_id={}",
                  client_id, packet_id));
}

void InflightStore::remove(std::string_view client_id, uint16_t packet_id,
                           InflightDirection direction) {
  const auto map_iter = entries_.find(std::string(client_id));
  if (map_iter == entries_.end()) {
    return;
  }
  auto &list = map_iter->second;
  const auto entry_iter = find_entry(list, packet_id, direction);
  if (entry_iter == list.end()) {
    return;
  }
  list.erase(entry_iter);
  if (list.empty()) {
    entries_.erase(map_iter);
  }
}

std::vector<InflightEntry>
InflightStore::entries_for(std::string_view client_id) const {
  const auto map_iter = entries_.find(std::string(client_id));
  if (map_iter == entries_.end()) {
    return {};
  }
  return map_iter->second;
}

bool InflightStore::is_packet_id_in_use(
    std::string_view client_id, uint16_t packet_id,
    InflightDirection direction) const noexcept {
  const auto map_iter = entries_.find(std::string(client_id));
  if (map_iter == entries_.end()) {
    return false;
  }
  const auto &list = map_iter->second;
  return std::any_of(list.begin(), list.end(),
                     [packet_id, direction](const InflightEntry &ent) {
                       return ent.packet_id == packet_id &&
                              ent.direction == direction;
                     });
}

std::size_t InflightStore::size_for(std::string_view client_id) const noexcept {
  const auto map_iter = entries_.find(std::string(client_id));
  if (map_iter == entries_.end()) {
    return 0U;
  }
  return map_iter->second.size();
}

} // namespace mqtt
