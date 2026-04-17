#pragma once

/**
 * @file keep_alive_timer.h
 * @brief KeepAliveTimer — deadline tracking for MQTT Keep Alive enforcement
 * (Module 7.2).
 */

#include <chrono>
#include <cstdint>

namespace mqtt {

/**
 * @brief Tracks the Keep Alive deadline for a single MQTT client connection
 * (Module 7.2).
 *
 * The MQTT specification requires the broker to disconnect a client that sends
 * no packet within 1.5 times the Keep Alive interval negotiated in CONNECT
 * (MQTT 5.0 §3.1.2.10).
 *
 * A Keep Alive value of 0 disables the timer entirely — `is_expired()` always
 * returns `false` and `is_enabled()` returns `false`.
 *
 * ### Usage
 * 1. Construct with the Keep Alive value from the CONNECT packet.
 * 2. Call `reset()` on every incoming packet.
 * 3. Poll `is_expired()` periodically; if `true`, close the connection.
 *
 * Thread safety: none — external synchronisation required.
 */
class KeepAliveTimer {
public:
  /**
   * @brief Construct and arm the timer.
   *
   * If `keep_alive_seconds` is 0 the timer is disabled and `is_expired()`
   * always returns `false`.
   *
   * @param keep_alive_seconds Keep Alive value from the CONNECT packet
   * (seconds).
   */
  explicit KeepAliveTimer(uint16_t keep_alive_seconds) noexcept;

  /**
   * @brief Reset the deadline to now + 1.5 × keep_alive (7.2.2).
   *
   * Call this on every incoming packet. No-op if the timer is disabled.
   */
  void reset() noexcept;

  /**
   * @brief Return true if the deadline has passed (7.2.3).
   *
   * Always returns `false` when the timer is disabled.
   *
   * @return `true` if the client has not sent a packet within the allowed
   * interval.
   */
  [[nodiscard]] bool is_expired() const noexcept;

  /**
   * @brief Return true if the timer is active (keep_alive > 0).
   * @return `false` when Keep Alive was negotiated as 0.
   */
  [[nodiscard]] bool is_enabled() const noexcept;

private:
  using Clock = std::chrono::steady_clock;

  std::chrono::milliseconds interval_ms_; ///< 1.5 × keep_alive in milliseconds.
  Clock::time_point deadline_; ///< Absolute deadline for the current window.
  bool enabled_;               ///< False when keep_alive == 0.
};

} // namespace mqtt
