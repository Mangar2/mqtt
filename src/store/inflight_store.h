#pragma once

/**
 * @file inflight_store.h
 * @brief In-memory inflight entry store (Module 4.4).
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <atomic>
#include <unordered_map>
#include <array>
#include <chrono>
#include <queue>
#include <vector>

#include "store/store_error.h"

#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"

namespace mqtt {

/**
 * @brief In-memory store for QoS 1 and QoS 2 in-flight message records
 * (Module 4.4).
 *
 * Entries are keyed by `(client_id, packet_id, direction)`.  One entry
 * represents one pending acknowledgement in the handshake state machine
 * (Module 5).
 *
 * Thread safety: all public methods are internally synchronized.
 */
class InflightStore {
public:
  /**
   * @brief Add a new inflight entry for a session (4.4.1).
   *
   * @param client_id Identifier of the owning session.
   * @param entry     Entry to store; `entry.packet_id` must be non-zero and
   *                  unique within `(client_id, entry.direction)`.
   */
  void create(std::string_view client_id, InflightEntry &&entry);

  /**
   * @brief Compatibility overload for existing call sites.
   * @param client_id Identifier of the owning session.
   * @param entry Entry to store by copy.
   */
  void create(std::string_view client_id, const InflightEntry &entry) {
    InflightEntry copied_entry = entry;
    create(client_id, std::move(copied_entry));
  }

  /**
   * @brief Advance the handshake state of an existing entry (4.4.2).
   *
   * @param client_id  Identifier of the owning session.
   * @param packet_id  Packet Identifier of the entry to update.
   * @param direction  Direction of the exchange.
   * @param new_state  New handshake state to set.
   * @throws StoreException(PacketIdNotFound) if no matching entry exists.
   */
  void update(std::string_view client_id, uint16_t packet_id,
              InflightDirection direction, InflightState new_state);

  /**
   * @brief Remove a completed inflight entry (4.4.3).
   *
   * Does nothing when no matching entry is found.
   *
   * @param client_id Identifier of the owning session.
   * @param packet_id Packet Identifier of the entry to remove.
   * @param direction Direction of the exchange.
   */
  void remove(std::string_view client_id, uint16_t packet_id,
              InflightDirection direction) noexcept;

  /**
   * @brief Visit a single entry when present.
   *
   * @tparam Visitor Callable receiving `const InflightEntry&`.
   * @param client_id Identifier of the session.
   * @param packet_id Packet Identifier to visit.
   * @param direction Direction of the exchange.
   * @param visit Callback invoked once when the entry exists.
   * @return `true` when the entry was found.
   */
  template <class Visitor>
  bool with_entry(std::string_view client_id, uint16_t packet_id,
                  InflightDirection direction, Visitor &&visit) const {
    const std::function<void(const InflightEntry &)> callback =
        [&visit](const InflightEntry &entry) { std::invoke(visit, entry); };
    return with_entry_impl(client_id, packet_id, direction, callback);
  }

  /**
   * @brief Iterate all live entries of one direction for a session.
   *
   * @tparam Visitor Callable receiving `const InflightEntry&`.
   * @param client_id Identifier of the session.
   * @param direction Direction to iterate.
   * @param visit Callback invoked for each live entry.
   */
  template <class Visitor>
  void for_each(std::string_view client_id, InflightDirection direction,
                Visitor &&visit) const {
    const std::function<void(const InflightEntry &)> callback =
        [&visit](const InflightEntry &entry) { std::invoke(visit, entry); };
    for_each_direction_impl(client_id, direction, callback);
  }

  /**
   * @brief Iterate all live entries of both directions for a session.
   *
   * @tparam Visitor Callable receiving `const InflightEntry&`.
   * @param client_id Identifier of the session.
   * @param visit Callback invoked for each live entry.
   */
  template <class Visitor>
  void for_each(std::string_view client_id, Visitor &&visit) const {
    const std::function<void(const InflightEntry &)> callback =
        [&visit](const InflightEntry &entry) { std::invoke(visit, entry); };
    for_each_all_impl(client_id, callback);
  }

  /**
   * @brief Iterate all entries across all sessions for persistence snapshots.
   *
   * @tparam Visitor Callable receiving `(std::string_view, const
   * InflightEntry&)`.
   * @param visit Callback invoked once per live entry.
   */
  template <class Visitor> void snapshot_each_session(Visitor &&visit) const {
    const std::function<void(std::string_view, const InflightEntry &)> callback =
        [&visit](std::string_view client_id, const InflightEntry &entry) {
          std::invoke(visit, client_id, entry);
        };
    snapshot_each_session_impl(callback);
  }

  /**
   * @brief Check whether a packet ID is currently registered (4.4.5).
   *
   * @param client_id Identifier of the session.
   * @param packet_id Packet Identifier to test.
   * @param direction Direction of the exchange.
   * @return `true` if an entry with the given `(packet_id, direction)` exists.
   */
  [[nodiscard]] bool
  is_packet_id_in_use(std::string_view client_id, uint16_t packet_id,
                      InflightDirection direction) const noexcept;

  /**
   * @brief Return the number of inflight entries for a session.
   * @param client_id Identifier of the session to query.
   * @return Entry count; zero when the session has no inflight messages.
   */
  [[nodiscard]] std::size_t size_for(std::string_view client_id) const noexcept;

  /**
   * @brief Return the number of inflight entries across all sessions.
   * @return Total inflight entry count.
   */
  [[nodiscard]] std::size_t total_size() const noexcept;

  /**
   * @brief Return outbound packet ids due for retransmission at/before cutoff.
   *
   * @param client_id Identifier of the session.
   * @param cutoff Retransmission cutoff timestamp.
   * @return Packet ids whose stored timestamp is `<= cutoff`.
   */
  [[nodiscard]] std::vector<uint16_t>
  due_outbound_packet_ids(std::string_view client_id,
                          std::chrono::steady_clock::time_point cutoff);

  /**
   * @brief Drop all inflight entries for a session.
   * @param client_id Identifier of the session to clear.
   */
  void drop_session(std::string_view client_id) noexcept;

private:
  static constexpr std::size_t k_shard_count_ = 64U;

  class InflightTable {
  public:
    void create(InflightEntry &&entry);
    [[nodiscard]] bool update_state(uint16_t packet_id, InflightState new_state);
    [[nodiscard]] bool remove(uint16_t packet_id) noexcept;
    [[nodiscard]] bool is_packet_id_in_use(uint16_t packet_id) const noexcept;
    [[nodiscard]] bool with_entry(
        uint16_t packet_id,
        const std::function<void(const InflightEntry &)> &visit) const;
    [[nodiscard]] std::vector<uint16_t>
    due_packet_ids(std::chrono::steady_clock::time_point cutoff);
    void for_each(const std::function<void(const InflightEntry &)> &visit) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t clear() noexcept;

  private:
    struct RetransmitCandidate {
      std::chrono::steady_clock::time_point timestamp;
      uint16_t packet_id;
      uint64_t generation;
    };

    struct RetransmitCandidateCompare {
      [[nodiscard]] bool operator()(const RetransmitCandidate &left,
                                    const RetransmitCandidate &right) const noexcept {
        if (left.timestamp != right.timestamp) {
          return left.timestamp > right.timestamp;
        }
        if (left.packet_id != right.packet_id) {
          return left.packet_id > right.packet_id;
        }
        return left.generation > right.generation;
      }
    };

    static constexpr std::size_t k_default_bucket_reserve_ = 32U;
    std::unordered_map<uint16_t, InflightEntry> entries_{};
    std::unordered_map<uint16_t, uint64_t> retransmit_generation_by_packet_{};
    std::priority_queue<RetransmitCandidate,
                        std::vector<RetransmitCandidate>,
                        RetransmitCandidateCompare>
        retransmit_queue_{};
    uint64_t next_retransmit_generation_{0U};
  };

  struct SessionSlot {
    mutable std::mutex session_entry_mutex;
    InflightTable outbound_table;
    InflightTable inbound_table;

    [[nodiscard]] InflightTable &table_for(InflightDirection direction) noexcept;
    [[nodiscard]] const InflightTable &
    table_for(InflightDirection direction) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t clear() noexcept;
  };

  struct Shard {
    mutable std::shared_mutex shard_mutex;
    std::unordered_map<std::string, std::shared_ptr<SessionSlot>> sessions;
  };

  [[nodiscard]] static std::size_t
  shard_index_for(std::string_view client_id) noexcept;
  [[nodiscard]] std::shared_ptr<SessionSlot>
  get_or_create_session(std::string_view client_id);
  [[nodiscard]] std::shared_ptr<SessionSlot>
  find_session(std::string_view client_id) const;

  [[nodiscard]] bool with_entry_impl(
      std::string_view client_id, uint16_t packet_id,
      InflightDirection direction,
      const std::function<void(const InflightEntry &)> &visit) const;
  void for_each_direction_impl(
      std::string_view client_id, InflightDirection direction,
      const std::function<void(const InflightEntry &)> &visit) const;
  void for_each_all_impl(
      std::string_view client_id,
      const std::function<void(const InflightEntry &)> &visit) const;
  void snapshot_each_session_impl(
      const std::function<void(std::string_view, const InflightEntry &)> &
          visit) const;

  mutable std::array<Shard, k_shard_count_> session_shards_{};
  std::atomic<std::size_t> total_size_{0U};
};

} // namespace mqtt
