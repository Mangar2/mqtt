#pragma once

/**
 * @file session_expiry_scheduler.h
 * @brief SessionExpiryScheduler — tracks per-session disconnect timestamps and
 * reports sessions whose expiry deadline has passed (Module 10.3).
 */

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mqtt {

/**
 * @brief Maintains a set of pending session expiry timers (Module 10.3).
 *
 * Each timer entry records the disconnect timestamp and expiry interval for a
 * session.  `collect_expired` returns all Client IDs whose deadline has been
 * reached at the given reference time.
 *
 * Expiry rules (MQTT 5.0 Section 3.1.2.11.2):
 * - `expiry_interval == 0`:           expires immediately on disconnect.
 * - `expiry_interval == 0xFFFF'FFFF`: never expires.
 * - Otherwise: expires `expiry_interval` seconds after `disconnect_time`.
 *
 * Thread safety: none — external synchronisation required.
 */
class SessionExpiryScheduler {
public:
  /**
   * @brief Schedule a session expiry timer (10.3.1).
   *
   * Replaces any existing timer entry for @p client_id.
   *
   * @param client_id      Identifier of the session.
   * @param disconnect_time Wall-clock time of the disconnect event.
   * @param expiry_interval Session expiry interval in seconds.
   */
  void schedule(std::string_view client_id,
                std::chrono::steady_clock::time_point disconnect_time,
                uint32_t expiry_interval);

  /**
   * @brief Cancel a pending expiry timer (10.3.2).
   *
   * No-op when @p client_id has no scheduled timer.
   *
   * @param client_id Identifier of the session whose timer is cancelled.
   */
  void cancel(std::string_view client_id);

  /**
   * @brief Return Client IDs of sessions that have expired (10.3.3).
   *
   * Does not remove the entries; call `cancel` after processing each
   * returned Client ID.  Entries with `expiry_interval == 0xFFFF'FFFF` are
   * never returned.
   *
   * @param now Reference time for deadline comparison.
   * @return Vector of Client IDs whose expiry deadline is ≤ @p now.
   */
  [[nodiscard]] std::vector<std::string> collect_expired(std::chrono::steady_clock::time_point now) const;

  /**
   * @brief Return the number of pending timer entries.
   * @return Timer count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  /**
   * @brief One scheduled session expiry timer entry.
   */
  struct TimerEntry {
    std::chrono::steady_clock::time_point disconnect_time;
    uint32_t expiry_interval;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, TimerEntry> timers_;
};

} // namespace mqtt
