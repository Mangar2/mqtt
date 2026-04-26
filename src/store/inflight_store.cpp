#include "store/inflight_store.h"

#include <format>
#include <utility>
#include <vector>

namespace mqtt {

void InflightStore::InflightTable::create(InflightEntry &&entry) {
  if (entry.packet_id == 0U) {
    throw StoreException(StoreError::InvalidPacketId,
                         "inflight create with packet_id=0");
  }

  if (entries_.empty()) {
    entries_.reserve(k_default_bucket_reserve_);
  }

  const uint16_t packet_id = entry.packet_id;
  const auto [iter, inserted] = entries_.try_emplace(packet_id, std::move(entry));
  (void)iter;
  if (!inserted) {
    throw StoreException(StoreError::PacketIdAlreadyInUse,
                         std::format("inflight packet_id already in use: {}",
                                     packet_id));
  }
}

bool InflightStore::InflightTable::update_state(uint16_t packet_id,
                                                InflightState new_state) {
  const auto entry_iter = entries_.find(packet_id);
  if (entry_iter == entries_.end()) {
    return false;
  }
  entry_iter->second.state = new_state;
  return true;
}

bool InflightStore::InflightTable::remove(uint16_t packet_id) noexcept {
  return entries_.erase(packet_id) != 0U;
}

bool InflightStore::InflightTable::is_packet_id_in_use(uint16_t packet_id) const noexcept {
  return entries_.contains(packet_id);
}

bool InflightStore::InflightTable::with_entry(
    uint16_t packet_id,
    const std::function<void(const InflightEntry &)> &visit) const {
  const auto entry_iter = entries_.find(packet_id);
  if (entry_iter == entries_.end()) {
    return false;
  }

  visit(entry_iter->second);
  return true;
}

void InflightStore::InflightTable::for_each(
    const std::function<void(const InflightEntry &)> &visit) const {
  for (const auto &entry_record : entries_) {
    visit(entry_record.second);
  }
}

std::size_t InflightStore::InflightTable::size() const noexcept {
  return entries_.size();
}

std::size_t InflightStore::InflightTable::clear() noexcept {
  const std::size_t removed_entries = entries_.size();
  entries_.clear();
  return removed_entries;
}

InflightStore::InflightTable &
InflightStore::SessionSlot::table_for(InflightDirection direction) noexcept {
  if (direction == InflightDirection::Outbound) {
    return outbound_table;
  }
  return inbound_table;
}

const InflightStore::InflightTable &
InflightStore::SessionSlot::table_for(InflightDirection direction) const noexcept {
  if (direction == InflightDirection::Outbound) {
    return outbound_table;
  }
  return inbound_table;
}

std::size_t InflightStore::SessionSlot::size() const noexcept {
  return outbound_table.size() + inbound_table.size();
}

bool InflightStore::SessionSlot::empty() const noexcept {
  return size() == 0U;
}

std::size_t InflightStore::SessionSlot::clear() noexcept {
  const std::size_t outbound_removed = outbound_table.clear();
  const std::size_t inbound_removed = inbound_table.clear();
  return outbound_removed + inbound_removed;
}

std::size_t InflightStore::shard_index_for(std::string_view client_id) noexcept {
  return std::hash<std::string_view>{}(client_id) & (k_shard_count_ - 1U);
}

std::shared_ptr<InflightStore::SessionSlot>
InflightStore::get_or_create_session(std::string_view client_id) {
  const std::size_t shard_index = shard_index_for(client_id);
  Shard &shard = session_shards_[shard_index];

  {
    const std::shared_lock shard_lock(shard.shard_mutex);
    const auto session_iter = shard.sessions.find(std::string(client_id));
    if (session_iter != shard.sessions.end()) {
      return session_iter->second;
    }
  }

  const std::unique_lock shard_lock(shard.shard_mutex);
  const auto session_iter = shard.sessions.find(std::string(client_id));
  if (session_iter != shard.sessions.end()) {
    return session_iter->second;
  }

  auto created_slot = std::make_shared<SessionSlot>();
  const auto [insert_iter, inserted] =
      shard.sessions.try_emplace(std::string(client_id), created_slot);
  if (!inserted) {
    return insert_iter->second;
  }
  return created_slot;
}

std::shared_ptr<InflightStore::SessionSlot>
InflightStore::find_session(std::string_view client_id) const {
  const std::size_t shard_index = shard_index_for(client_id);
  const Shard &shard = session_shards_[shard_index];
  const std::shared_lock shard_lock(shard.shard_mutex);
  const auto session_iter = shard.sessions.find(std::string(client_id));
  if (session_iter == shard.sessions.end()) {
    return nullptr;
  }
  return session_iter->second;
}

void InflightStore::create(std::string_view client_id, InflightEntry &&entry) {
  if (entry.packet_id == 0U) {
    throw StoreException(StoreError::InvalidPacketId,
                         "inflight create with packet_id=0");
  }

  const std::shared_ptr<SessionSlot> session = get_or_create_session(client_id);
  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  InflightTable &table = session->table_for(entry.direction);
  table.create(std::move(entry));
  total_size_.fetch_add(1U, std::memory_order_relaxed);
}

void InflightStore::update(std::string_view client_id, uint16_t packet_id,
                           InflightDirection direction,
                           InflightState new_state) {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    throw StoreException(
        StoreError::PacketIdNotFound,
        std::format("inflight entry not found: client={}, packet_id={}",
                    client_id, packet_id));
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  InflightTable &table = session->table_for(direction);
  if (!table.update_state(packet_id, new_state)) {
    throw StoreException(
        StoreError::PacketIdNotFound,
        std::format("inflight entry not found: client={}, packet_id={}",
                    client_id, packet_id));
  }
}

void InflightStore::remove(std::string_view client_id, uint16_t packet_id,
                           InflightDirection direction) noexcept {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  InflightTable &table = session->table_for(direction);
  if (table.remove(packet_id)) {
    total_size_.fetch_sub(1U, std::memory_order_relaxed);
  }
}

bool InflightStore::with_entry_impl(
    std::string_view client_id, uint16_t packet_id,
    InflightDirection direction,
    const std::function<void(const InflightEntry &)> &visit) const {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return false;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  const InflightTable &table = session->table_for(direction);
  return table.with_entry(packet_id, visit);
}

void InflightStore::for_each_direction_impl(
    std::string_view client_id, InflightDirection direction,
    const std::function<void(const InflightEntry &)> &visit) const {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  const InflightTable &table = session->table_for(direction);
  table.for_each(visit);
}

void InflightStore::for_each_all_impl(
    std::string_view client_id,
    const std::function<void(const InflightEntry &)> &visit) const {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  session->outbound_table.for_each(visit);
  session->inbound_table.for_each(visit);
}

void InflightStore::snapshot_each_session_impl(
    const std::function<void(std::string_view, const InflightEntry &)> &
        visit) const {
  for (const Shard &shard : session_shards_) {
    std::vector<std::pair<std::string, std::shared_ptr<SessionSlot>>>
        session_snapshot;
    {
      const std::shared_lock shard_lock(shard.shard_mutex);
      session_snapshot.reserve(shard.sessions.size());
      for (const auto &session_record : shard.sessions) {
        session_snapshot.push_back(session_record);
      }
    }

    for (const auto &session_record : session_snapshot) {
      const std::string &client_identifier = session_record.first;
      const std::shared_ptr<SessionSlot> &session = session_record.second;
      const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
      session->outbound_table.for_each(
          [&visit, &client_identifier](const InflightEntry &entry) {
            visit(client_identifier, entry);
          });
      session->inbound_table.for_each(
          [&visit, &client_identifier](const InflightEntry &entry) {
            visit(client_identifier, entry);
          });
    }
  }
}

bool InflightStore::is_packet_id_in_use(
    std::string_view client_id, uint16_t packet_id,
    InflightDirection direction) const noexcept {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return false;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  const InflightTable &table = session->table_for(direction);
  return table.is_packet_id_in_use(packet_id);
}

std::size_t InflightStore::size_for(std::string_view client_id) const noexcept {
  const std::shared_ptr<SessionSlot> session = find_session(client_id);
  if (!session) {
    return 0U;
  }

  const std::lock_guard<std::mutex> session_lock(session->session_entry_mutex);
  return session->size();
}

std::size_t InflightStore::total_size() const noexcept {
  return total_size_.load(std::memory_order_relaxed);
}

void InflightStore::drop_session(std::string_view client_id) noexcept {
  const std::size_t shard_index = shard_index_for(client_id);
  Shard &shard = session_shards_[shard_index];

  std::shared_ptr<SessionSlot> removed_session;
  {
    const std::unique_lock shard_lock(shard.shard_mutex);
    const auto session_iter = shard.sessions.find(std::string(client_id));
    if (session_iter == shard.sessions.end()) {
      return;
    }
    removed_session = session_iter->second;
    shard.sessions.erase(session_iter);
  }

  std::size_t removed_entries = 0U;
  {
    const std::lock_guard<std::mutex> session_lock(removed_session->session_entry_mutex);
    removed_entries = removed_session->clear();
  }

  if (removed_entries != 0U) {
    total_size_.fetch_sub(removed_entries, std::memory_order_relaxed);
  }
}

} // namespace mqtt
