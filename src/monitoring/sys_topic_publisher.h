#pragma once

/**
 * @file sys_topic_publisher.h
 * @brief SysTopicPublisher — periodic publication of broker statistics to
 *        $SYS topics (Module 16.2).
 */

#include <chrono>
#include <functional>

#include "data_model/message/message.h"
#include "monitoring/statistics_collector.h"

namespace mqtt {

/**
 * @brief Publishes broker statistics to standard `$SYS/broker/…` topics at a
 *        configurable interval (Module 16.2).
 *
 * The caller must invoke `tick()` regularly (e.g. from the main event loop).
 * On each invocation `tick()` checks whether the configured interval has
 * elapsed; if so, it takes a statistics snapshot and publishes one retained
 * message per `$SYS` topic.
 *
 * ### Published topics (16.2.1)
 *
 * | Topic                                  | Value                   |
 * |----------------------------------------|-------------------------|
 * | `$SYS/broker/clients/connected`        | Connected client count  |
 * | `$SYS/broker/messages/received`        | Total inbound messages  |
 * | `$SYS/broker/messages/sent`            | Total outbound messages |
 * | `$SYS/broker/subscriptions/count`      | Active subscriptions    |
 * | `$SYS/broker/retained messages/count`  | Retained message count  |
 * | `$SYS/broker/uptime`                   | Uptime in seconds       |
 *
 * All messages use QoS 0 and `retain = true` so that newly connecting
 * subscribers receive the last known value immediately.
 *
 * ### Interval configuration (16.2.2)
 * Interval is supplied at construction.  A value ≤ 0 disables publication.
 *
 * ### System topic exclusion (16.2.3)
 * All topics begin with `$SYS/`.  The Topic Engine's system-topic rule
 * (Module 3.3.4) already excludes them from `#` / `+` wildcard subscriptions —
 * no additional logic is required here.
 */
class SysTopicPublisher {
public:
  /**
   * @brief Callback used to deliver a single `$SYS` message to the broker's
   *        routing pipeline.
   *
   * @param msg Message to publish (topic, payload, QoS, retain).
   */
  using PublishFn = std::function<void(Message)>;

  /**
   * @brief Construct a SysTopicPublisher.
   *
   * @param stats      Statistics collector to read snapshots from.
   * @param interval   How often to publish statistics (16.2.2).
   *                   A value ≤ 0 disables publication.
   * @param publish_fn Callback that routes a `$SYS` message through the broker.
   */
  SysTopicPublisher(const StatisticsCollector &stats,
                    std::chrono::seconds interval, PublishFn publish_fn);

  /**
   * @brief Check elapsed time and publish statistics if the interval has
   *        elapsed (16.2.1 + 16.2.2).
   *
   * Safe to call at any frequency.  Publication is skipped when the interval
   * has not yet elapsed or when `interval ≤ 0`.
   *
   * @param now Current time reference; defaults to `steady_clock::now()`.
   * @return `true` if statistics were published during this call.
   */
  bool tick(std::chrono::steady_clock::time_point now =
                std::chrono::steady_clock::now());

private:
  /// Publish all `$SYS` topics for the given snapshot.
  void publish_stats(const StatisticsSnapshot &snap);

  /// Build and emit a single `$SYS` message with a decimal numeric payload.
  void publish_one(std::string_view topic_name, std::uint64_t value);

  const StatisticsCollector &stats_; ///< Source of statistics snapshots.
  std::chrono::seconds interval_;    ///< Minimum interval between publishes.
  PublishFn publish_fn_;             ///< Routes a message through the broker.

  /// Time of the last successful publish; epoch = never published.
  std::chrono::steady_clock::time_point last_publish_{};
};

} // namespace mqtt
