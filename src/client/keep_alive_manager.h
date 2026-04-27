#pragma once

/**
 * @file keep_alive_manager.h
 * @brief Active client-side keep-alive manager for PINGREQ/PINGRESP cycles.
 */

#include <chrono>
#include <cstdint>

#include "codec/write_buffer.h"

namespace mqtt {

/**
 * @brief Outcome of one keep-alive polling step.
 */
enum class KeepAliveAction : uint8_t {
  None,       ///< No action required.
  SendPingreq,///< Send one PINGREQ now.
  Timeout,    ///< Connection considered dead due to missing PINGRESP.
};

/**
 * @brief Active keep-alive state machine for outbound MQTT client sessions.
 *
 * Behavior:
 * - If idle for keep-alive interval, emits `SendPingreq` once.
 * - After PINGREQ, waits for PINGRESP until response timeout.
 * - Emits `Timeout` when no PINGRESP arrives in time.
 */
class KeepAliveManager {
public:
  /**
   * @brief Construct a keep-alive manager.
   * @param keep_alive_seconds Negotiated keep-alive interval in seconds.
   * @param pingresp_timeout Maximum wait time after sending PINGREQ.
   */
  explicit KeepAliveManager(
      uint16_t keep_alive_seconds,
      std::chrono::milliseconds pingresp_timeout = std::chrono::milliseconds{0});

  /** @brief Clock type used by the manager. */
  using Clock = std::chrono::steady_clock;

  /**
   * @brief Record traffic activity and clear any pending idle window.
   * @param now Observation timestamp.
   */
  void note_activity(Clock::time_point now = Clock::now()) noexcept;

  /**
   * @brief Record receipt of a matching PINGRESP.
   * @param now Observation timestamp.
   */
  void on_pingresp(Clock::time_point now = Clock::now()) noexcept;

  /**
   * @brief Evaluate keep-alive timers and return required action.
   * @param now Evaluation timestamp.
   * @return Action for caller to execute.
   */
  [[nodiscard]] KeepAliveAction poll(Clock::time_point now = Clock::now()) noexcept;

  /**
   * @brief Return whether the manager is enabled.
   */
  [[nodiscard]] bool is_enabled() const noexcept;

  /**
   * @brief Return whether a PINGRESP is currently expected.
   */
  [[nodiscard]] bool awaiting_pingresp() const noexcept;

  /**
   * @brief Build one MQTT PINGREQ frame.
   * @return Wire-ready PINGREQ bytes.
   */
  [[nodiscard]] static WriteBuffer encode_pingreq_frame();

private:
  std::chrono::milliseconds keep_alive_interval_;
  std::chrono::milliseconds pingresp_timeout_;
  Clock::time_point last_activity_;
  Clock::time_point ping_deadline_;
  bool enabled_{false};
  bool ping_outstanding_{false};
};

} // namespace mqtt
