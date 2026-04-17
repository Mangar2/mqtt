#pragma once

/**
 * @file statistics_collector.h
 * @brief StatisticsCollector — runtime observability counters for the broker
 *        (Module 16.1).
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "store/retained_message_store.h"
#include "store/subscription_store.h"

namespace mqtt {

/**
 * @brief Snapshot of broker statistics at a single point in time (Module 16.1).
 */
struct StatisticsSnapshot {
  /// Number of currently connected clients (16.1.1).
  std::size_t connected_clients{0};

  /// Total PUBLISH messages received since broker startup (16.1.2).
  std::uint64_t messages_inbound{0};

  /// Total messages delivered to subscribers since broker startup (16.1.2).
  std::uint64_t messages_outbound{0};

  /// Total active subscriptions across all clients (16.1.3).
  std::size_t active_subscriptions{0};

  /// Total stored retained messages (16.1.4).
  std::size_t retained_messages{0};

  /// Seconds elapsed since the broker started (16.1.5).
  std::chrono::seconds uptime{0};
};

/**
 * @brief Collects and exposes runtime statistics for the MQTT broker
 *        (Module 16.1).
 *
 * Maintains atomic counters for connected clients and message throughput.
 * Reads subscription and retained message counts directly from the respective
 * stores at snapshot time.
 *
 * Thread safety: all `on_*` methods and `snapshot()` are safe to call
 * concurrently.  Store queries are read-only and safe when the broker is
 * single-threaded at the store level.
 */
class StatisticsCollector {
public:
  /**
   * @brief Construct a StatisticsCollector.
   *
   * @param sub_store      SubscriptionStore for querying active subscription
   *                       count (16.1.3).
   * @param retained_store RetainedMessageStore for querying retained message
   *                       count (16.1.4).
   */
  StatisticsCollector(const SubscriptionStore &sub_store,
                      const RetainedMessageStore &retained_store);

  /**
   * @brief Increment the connected-client counter (16.1.1).
   *
   * Call when a client successfully completes the CONNECT handshake.
   */
  void on_client_connected() noexcept;

  /**
   * @brief Decrement the connected-client counter (16.1.1).
   *
   * Call when a client disconnects or its connection is closed.
   */
  void on_client_disconnected() noexcept;

  /**
   * @brief Increment the inbound message counter (16.1.2).
   *
   * Call once for every PUBLISH message that enters the routing pipeline.
   */
  void on_message_inbound() noexcept;

  /**
   * @brief Increment the outbound message counter (16.1.2).
   *
   * Call once for every message delivered to an online or offline subscriber.
   */
  void on_message_outbound() noexcept;

  /**
   * @brief Build a snapshot of all current statistics (16.1.1–16.1.5).
   *
   * Queries the stores for live counts and copies atomic counter values.
   *
   * @return `StatisticsSnapshot` with current values.
   */
  [[nodiscard]] StatisticsSnapshot snapshot() const noexcept;

private:
  const SubscriptionStore &sub_store_;         ///< Used for subscription count.
  const RetainedMessageStore &retained_store_; ///< Used for retained msg count.

  std::atomic<std::size_t> connected_clients_{0U};   ///< Live connection count.
  std::atomic<std::uint64_t> messages_inbound_{0U};  ///< Cumulative inbound.
  std::atomic<std::uint64_t> messages_outbound_{0U}; ///< Cumulative outbound.

  /// Timestamp recorded at construction — used to compute uptime (16.1.5).
  std::chrono::steady_clock::time_point start_time_{
      std::chrono::steady_clock::now()};
};

} // namespace mqtt
