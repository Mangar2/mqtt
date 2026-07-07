#pragma once

/**
 * @file message_expiry_controller.h
 * @brief MessageExpiryController — enforces Message Expiry Interval for
 *        queued and in-flight messages (Module 12.4).
 */

#include <chrono>

#include "data_model/message/message.h"

namespace mqtt {

/**
 * @brief Enforces MQTT 5.0 Message Expiry Interval semantics (Module 12.4).
 *
 * The Message Expiry Interval property (ID 0x02) carries the lifetime of a
 * published message in seconds.  The broker must:
 * - Discard the message when the interval has elapsed before delivery
 *   (12.4.2).
 * - Reduce the remaining interval to reflect the time the broker held the
 *   message before forwarding it (12.4.3).
 *
 * Thread safety: none required — all methods are static.
 */
class MessageExpiryController {
public:
  /**
   * @brief Check whether a queued message is still valid and update its
   *        remaining expiry interval (12.4.1–12.4.3).
   *
   * When the message carries a MessageExpiryInterval property:
   * - The elapsed seconds since @p enqueue_time are subtracted from the
   *   original interval.
   * - If the result is 0 or the message has already expired, the call
   *   returns `false`.
   * - Otherwise the MessageExpiryInterval property is updated in-place to
   *   the remaining seconds and the call returns `true`.
   *
   * When the message carries no MessageExpiryInterval property, `true` is
   * returned immediately (no expiry).
   *
   * @param msg          Message to check and update.
   * @param enqueue_time Time at which the message was received or enqueued.
   * @param now          Current time; defaults to steady_clock::now().
   * @return `true` when the message is still valid; `false` when it has
   *         expired and must be discarded.
   */
    [[nodiscard]] static bool update_expiry(
      Message &msg, std::chrono::steady_clock::time_point enqueue_time,
      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
};

} // namespace mqtt
