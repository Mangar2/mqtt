#pragma once

/**
 * @file will_delay_timer.h
 * @brief Will Delay Timer — per-client delay tracking for will publication
 * (Module 11.2).
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mqtt {

/**
 * @brief Tracks pending will-delay timers for MQTT 5.0 clients (Module 11.2).
 *
 * Each entry records when a connection was closed and the Will Delay Interval
 * (MQTT 5.0 Will Properties, identifier 0x18).  `collect_due` returns all
 * Client IDs whose delay has elapsed.
 *
 * Will Delay semantics:
 * - `delay_seconds == 0`: entry is due immediately (≥ `disconnect_time`).
 * - Any positive value: entry is due `delay_seconds` seconds after
 *   `disconnect_time`.
 *
 * Thread safety: none — external synchronisation required.
 */
class WillDelayTimer {
public:
  /**
   * @brief Arm (or replace) the delay timer for a client (11.2.1).
   *
   * @param client_id       Client identifier.
   * @param disconnect_time Wall-clock time at which the connection closed.
   * @param delay_seconds   Will Delay Interval in seconds (property 0x18).
   */
  void schedule(std::string_view client_id,
                std::chrono::steady_clock::time_point disconnect_time,
                uint32_t delay_seconds);

  /**
   * @brief Cancel the delay timer for a client (11.2.2).
   *
   * No-op when @p client_id has no scheduled timer.
   *
   * @param client_id Client identifier whose timer is to be removed.
   */
  void cancel(std::string_view client_id);

  /**
   * @brief Return all Client IDs whose delay timer has fired (11.2.1).
   *
   * An entry fires when `disconnect_time + delay_seconds ≤ now`.
   * Does **not** remove entries; call `cancel` for each returned Client ID
   * after processing.
   *
   * @param now Reference time for deadline comparison.
   * @return Vector of Client IDs whose delay has elapsed.
   */
  [[nodiscard]] std::vector<std::string>
  collect_due(std::chrono::steady_clock::time_point now) const;

  /**
   * @brief Return the number of pending timer entries.
   * @return Timer count.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  /// Per-client timer entry.
  struct TimerEntry {
    std::chrono::steady_clock::time_point
        disconnect_time;    ///< When the connection closed.
    uint32_t delay_seconds; ///< Will Delay Interval in seconds.
  };

  std::unordered_map<std::string, TimerEntry>
      timers_; ///< Pending timers keyed by Client ID.
};

} // namespace mqtt
