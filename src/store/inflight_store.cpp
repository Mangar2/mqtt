#include "store/inflight_store.h"

#include <bit>
#include <format>
#include <utility>
#include <vector>

namespace mqtt {

namespace {

constexpr int32_t k_no_active_chunk = -1;

} // namespace

std::pair<std::size_t, std::size_t>
InflightStore::InflightTable::chunk_and_slot_indices(uint16_t packet_id) {
  if (packet_id == 0U) {
    throw StoreException(StoreError::InvalidPacketId,
                         "inflight create with packet_id=0");
  }

  const std::size_t packet_offset = static_cast<std::size_t>(packet_id - 1U);
  const std::size_t chunk_index = packet_offset / k_slots_per_chunk_;
  const std::size_t slot_index = packet_offset % k_slots_per_chunk_;
  return {chunk_index, slot_index};
}

uint64_t InflightStore::InflightTable::slot_mask(std::size_t slot_index) noexcept {
  return 1ULL << slot_index;
}

void InflightStore::InflightTable::ensure_chunk(std::size_t chunk_index) {
  if (chunks_[chunk_index]) {
    return;
  }

  if (!free_chunks_.empty()) {
    chunks_[chunk_index] = std::move(free_chunks_.back());
    free_chunks_.pop_back();
  } else {
    chunks_[chunk_index] = std::make_unique<Chunk>();
  }

  Chunk &chunk = *chunks_[chunk_index];
  chunk.occupancy_bitmap = 0ULL;
  chunk.live_slot_count = 0U;
  chunk.base_packet_id = static_cast<uint16_t>(chunk_index * k_slots_per_chunk_ + 1U);
  for (auto &slot_entry : chunk.slots) {
    slot_entry.reset();
  }
}

void InflightStore::InflightTable::recycle_chunk(std::size_t chunk_index) noexcept {
  std::unique_ptr<Chunk> chunk = std::move(chunks_[chunk_index]);
  if (!chunk) {
    return;
  }

  chunk->occupancy_bitmap = 0ULL;
  chunk->live_slot_count = 0U;
  chunk->base_packet_id = 0U;
  for (auto &slot_entry : chunk->slots) {
    slot_entry.reset();
  }

  if (free_chunks_.size() < k_free_list_max_) {
    free_chunks_.push_back(std::move(chunk));
  }
}

void InflightStore::InflightTable::link_active_chunk(std::size_t chunk_index) noexcept {
  const int32_t linked_chunk_index = static_cast<int32_t>(chunk_index);
  const int32_t old_tail_index = last_active_chunk_index_;
  prev_active_chunk_indices_[chunk_index] = old_tail_index;
  next_active_chunk_indices_[chunk_index] = k_no_active_chunk;

  if (old_tail_index == k_no_active_chunk) {
    first_active_chunk_index_ = linked_chunk_index;
  } else {
    next_active_chunk_indices_[static_cast<std::size_t>(old_tail_index)] =
        linked_chunk_index;
  }
  last_active_chunk_index_ = linked_chunk_index;
}

void InflightStore::InflightTable::unlink_active_chunk(std::size_t chunk_index) noexcept {
  const int32_t previous_chunk_index = prev_active_chunk_indices_[chunk_index];
  const int32_t next_chunk_index = next_active_chunk_indices_[chunk_index];

  if (previous_chunk_index == k_no_active_chunk) {
    first_active_chunk_index_ = next_chunk_index;
  } else {
    next_active_chunk_indices_[static_cast<std::size_t>(previous_chunk_index)] =
        next_chunk_index;
  }

  if (next_chunk_index == k_no_active_chunk) {
    last_active_chunk_index_ = previous_chunk_index;
  } else {
    prev_active_chunk_indices_[static_cast<std::size_t>(next_chunk_index)] =
        previous_chunk_index;
  }

  prev_active_chunk_indices_[chunk_index] = k_no_active_chunk;
  next_active_chunk_indices_[chunk_index] = k_no_active_chunk;
}

void InflightStore::InflightTable::create(InflightEntry &&entry) {
  const auto [chunk_index, slot_index] = chunk_and_slot_indices(entry.packet_id);
  ensure_chunk(chunk_index);

  Chunk &chunk = *chunks_[chunk_index];
  const uint64_t occupancy_mask = slot_mask(slot_index);
  if ((chunk.occupancy_bitmap & occupancy_mask) != 0ULL) {
    throw StoreException(StoreError::PacketIdAlreadyInUse,
                         std::format("inflight packet_id already in use: {}",
                                     entry.packet_id));
  }

  chunk.slots[slot_index].emplace(std::move(entry));
  chunk.occupancy_bitmap |= occupancy_mask;
  if (chunk.live_slot_count == 0U) {
    link_active_chunk(chunk_index);
  }
  chunk.live_slot_count = static_cast<uint16_t>(chunk.live_slot_count + 1U);
  active_count_ += 1U;
}

bool InflightStore::InflightTable::update_state(uint16_t packet_id,
                                                InflightState new_state) {
  const auto [chunk_index, slot_index] = chunk_and_slot_indices(packet_id);
  const std::unique_ptr<Chunk> &chunk_ptr = chunks_[chunk_index];
  if (!chunk_ptr) {
    return false;
  }

  const uint64_t occupancy_mask = slot_mask(slot_index);
  Chunk &chunk = *chunk_ptr;
  if ((chunk.occupancy_bitmap & occupancy_mask) == 0ULL) {
    return false;
  }

  chunk.slots[slot_index]->state = new_state;
  return true;
}

bool InflightStore::InflightTable::remove(uint16_t packet_id) noexcept {
  if (packet_id == 0U) {
    return false;
  }

  const std::size_t packet_offset = static_cast<std::size_t>(packet_id - 1U);
  const std::size_t chunk_index = packet_offset / k_slots_per_chunk_;
  const std::size_t slot_index = packet_offset % k_slots_per_chunk_;
  const std::unique_ptr<Chunk> &chunk_ptr = chunks_[chunk_index];
  if (!chunk_ptr) {
    return false;
  }

  const uint64_t occupancy_mask = slot_mask(slot_index);
  Chunk &chunk = *chunk_ptr;
  if ((chunk.occupancy_bitmap & occupancy_mask) == 0ULL) {
    return false;
  }

  chunk.slots[slot_index].reset();
  chunk.occupancy_bitmap &= ~occupancy_mask;
  chunk.live_slot_count = static_cast<uint16_t>(chunk.live_slot_count - 1U);
  active_count_ -= 1U;
  if (chunk.live_slot_count == 0U) {
    unlink_active_chunk(chunk_index);
    recycle_chunk(chunk_index);
  }

  return true;
}

bool InflightStore::InflightTable::is_packet_id_in_use(uint16_t packet_id) const noexcept {
  if (packet_id == 0U) {
    return false;
  }

  const std::size_t packet_offset = static_cast<std::size_t>(packet_id - 1U);
  const std::size_t chunk_index = packet_offset / k_slots_per_chunk_;
  const std::size_t slot_index = packet_offset % k_slots_per_chunk_;
  const std::unique_ptr<Chunk> &chunk_ptr = chunks_[chunk_index];
  if (!chunk_ptr) {
    return false;
  }

  return (chunk_ptr->occupancy_bitmap & slot_mask(slot_index)) != 0ULL;
}

bool InflightStore::InflightTable::with_entry(
    uint16_t packet_id,
    const std::function<void(const InflightEntry &)> &visit) const {
  if (packet_id == 0U) {
    return false;
  }

  const std::size_t packet_offset = static_cast<std::size_t>(packet_id - 1U);
  const std::size_t chunk_index = packet_offset / k_slots_per_chunk_;
  const std::size_t slot_index = packet_offset % k_slots_per_chunk_;
  const std::unique_ptr<Chunk> &chunk_ptr = chunks_[chunk_index];
  if (!chunk_ptr) {
    return false;
  }

  const uint64_t occupancy_mask = slot_mask(slot_index);
  const Chunk &chunk = *chunk_ptr;
  if ((chunk.occupancy_bitmap & occupancy_mask) == 0ULL) {
    return false;
  }

  visit(*chunk.slots[slot_index]);
  return true;
}

void InflightStore::InflightTable::for_each(
    const std::function<void(const InflightEntry &)> &visit) const {
  int32_t active_chunk_index = first_active_chunk_index_;
  while (active_chunk_index != k_no_active_chunk) {
    const std::size_t chunk_index = static_cast<std::size_t>(active_chunk_index);
    const Chunk &chunk = *chunks_[chunk_index];
    uint64_t remaining_bits = chunk.occupancy_bitmap;
    while (remaining_bits != 0ULL) {
      const std::size_t slot_index =
          static_cast<std::size_t>(std::countr_zero(remaining_bits));
      visit(*chunk.slots[slot_index]);
      remaining_bits &= (remaining_bits - 1ULL);
    }
    active_chunk_index = next_active_chunk_indices_[chunk_index];
  }
}

std::size_t InflightStore::InflightTable::size() const noexcept {
  return active_count_;
}

std::size_t InflightStore::InflightTable::clear() noexcept {
  const std::size_t removed_entries = active_count_;
  for (std::size_t chunk_index = 0U; chunk_index < k_chunk_count_; ++chunk_index) {
    if (chunks_[chunk_index]) {
      recycle_chunk(chunk_index);
    }
    prev_active_chunk_indices_[chunk_index] = k_no_active_chunk;
    next_active_chunk_indices_[chunk_index] = k_no_active_chunk;
  }

  first_active_chunk_index_ = k_no_active_chunk;
  last_active_chunk_index_ = k_no_active_chunk;
  active_count_ = 0U;
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
