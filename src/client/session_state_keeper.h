#pragma once

/**
 * @file session_state_keeper.h
 * @brief Client-side session-state keeper for reconnect restore workflows.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "data_model/session/inflight_entry.h"
#include "data_model/session/session_state.h"

namespace mqtt {

class InflightStore;

/**
 * @brief Snapshot of client session state and outbound inflight entries.
 */
struct ClientSessionSnapshot {
  SessionState session_state;                 ///< Stored session metadata.
  std::vector<InflightEntry> outbound_inflight; ///< Stored outbound inflight entries.
};

/**
 * @brief Restore plan returned for reconnect handling.
 */
struct ClientSessionRestorePlan {
  std::vector<Subscription> subscriptions;    ///< Subscriptions to restore.
  std::vector<InflightEntry> outbound_inflight; ///< Outbound inflight exchanges to replay.
  uint32_t session_expiry_interval{0U};       ///< Session expiry to apply on reconnect.
};

/**
 * @brief Stores client-side session state required for reconnect recovery.
 *
 * Tracks:
 * - active subscriptions,
 * - outbound inflight QoS state,
 * - negotiated session expiry interval.
 */
class ClientSessionStateKeeper {
public:
  /**
   * @brief Construct a keeper for one client identifier.
   * @param client_id Client identifier owning this state.
   * @param session_expiry_interval Session expiry interval in seconds.
   */
  explicit ClientSessionStateKeeper(std::string client_id,
                                    uint32_t session_expiry_interval = 0U);

  /**
   * @brief Return the owning client identifier.
   */
  [[nodiscard]] std::string_view client_id() const noexcept;

  /**
   * @brief Set the stored session expiry interval.
   * @param session_expiry_interval Session expiry interval in seconds.
   */
  void set_session_expiry_interval(uint32_t session_expiry_interval) noexcept;

  /**
   * @brief Return the stored session expiry interval.
   */
  [[nodiscard]] uint32_t session_expiry_interval() const noexcept;

  /**
   * @brief Insert or replace one subscription by topic filter.
   * @param subscription Subscription record to upsert.
   */
  void upsert_subscription(const Subscription &subscription);

  /**
   * @brief Remove one subscription by topic filter.
   * @param topic_filter Topic filter key to remove.
   * @return True when a subscription was removed.
   */
  [[nodiscard]] bool remove_subscription(std::string_view topic_filter) noexcept;

  /**
   * @brief Remove all stored subscriptions.
   */
  void clear_subscriptions() noexcept;

  /**
   * @brief Return current subscription snapshot.
   */
  [[nodiscard]] const std::vector<Subscription> &subscriptions() const noexcept;

  /**
   * @brief Set outbound inflight entries from provided list.
   *
   * Inbound entries are ignored. Stored entries are sorted by timestamp and
   * packet identifier for deterministic replay order.
   *
   * @param entries Candidate inflight entries.
   */
  void set_outbound_inflight(std::vector<InflightEntry> entries);

  /**
   * @brief Capture outbound inflight entries from an InflightStore.
   * @param inflight_store Source inflight store.
   */
  void capture_outbound_inflight(const InflightStore &inflight_store);

  /**
   * @brief Return stored outbound inflight entries.
   */
  [[nodiscard]] const std::vector<InflightEntry> &outbound_inflight() const noexcept;

  /**
   * @brief Build restore plan for reconnect handling.
   * @param clean_start Clean-start flag of upcoming reconnect.
   * @return Restore plan; empty when clean_start is true.
   */
  [[nodiscard]] ClientSessionRestorePlan build_restore_plan(bool clean_start) const;

  /**
   * @brief Return full snapshot of stored state.
   */
  [[nodiscard]] ClientSessionSnapshot snapshot() const;

  /**
   * @brief Replace stored state from snapshot.
   * @param snapshot Snapshot to apply.
   * @throws ClientException when snapshot client_id does not match.
   */
  void apply_snapshot(const ClientSessionSnapshot &snapshot);

private:
  void sort_outbound_inflight() noexcept;

  SessionState session_state_;
  std::vector<InflightEntry> outbound_inflight_;
};

} // namespace mqtt
