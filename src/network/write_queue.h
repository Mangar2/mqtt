#pragma once

/**
 * @file write_queue.h
 * @brief WriteQueue — thread-safe outgoing packet queue with async drain
 * (Module 6.3).
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "network/tcp_connection.h"

namespace mqtt {

class StructuredTracer;

/**
 * @brief Thread-safe queue of outgoing serialized packets with asynchronous
 * drain capability (Module 6.3).
 *
 * ### Design
 * - `enqueue()` pushes a packet and wakes the drain thread (6.3.1).
 * - `run_drain()` blocks the *calling* thread, writing queued packets to the
 *   `TcpConnection` as they arrive; it exits when `stop()` is called (6.3.2).
 * - Backpressure (6.3.3): `enqueue()` returns `false` — without throwing —
 *   when the total queued byte count would exceed `max_bytes`.
 *
 * ### Intended usage
 * ```cpp
 * WriteQueue queue;
 * std::jthread writer{[&](std::stop_token){ queue.run_drain(conn); }};
 * queue.enqueue(pkt);   // from a different thread
 * queue.stop();         // when connection closes
 * ```
 *
 * Thread safety: `enqueue()`, `stop()`, and `run_drain()` are safe to call
 * concurrently from different threads.  `drain()` is a one-shot synchronous
 * helper intended for single-threaded use or testing.
 */
class WriteQueue {
public:
  /** @brief Default queue capacity in bytes (64 KiB). */
  static constexpr std::size_t k_default_max_bytes = 64U * 1024U;

  /**
   * @brief Construct a queue with a byte capacity limit.
   * @param max_bytes Maximum total bytes that may be buffered.
   */
  explicit WriteQueue(std::size_t max_bytes = k_default_max_bytes);

  /**
   * @brief Attach optional structured tracer metadata for queue diagnostics.
   *
   * @param tracer Structured tracer instance; nullptr disables queue tracing.
   * @param queue_id Stable queue identifier (for example client ID).
   */
  void set_tracer(StructuredTracer *tracer, std::string queue_id) noexcept;

  /**
   * @brief Enqueue one serialized packet (6.3.1).
   *
   * Wakes the drain thread if one is waiting.
   *
   * @param packet Fully serialized MQTT packet bytes.
   * @return `true` on success; `false` when the queue is full (backpressure).
   */
  [[nodiscard]] bool enqueue(std::vector<uint8_t> packet);

  /**
   * @brief Write all currently queued packets to `conn` and return (6.3.2).
   *
   * Drains the queue once synchronously.  Does not wait for new packets.
   * Intended for single-threaded tests/scenarios.
   *
   * @param conn Connection to write to.
   * @return `true` if all writes succeeded; `false` on the first write error.
   */
  bool drain(TcpConnection &conn);

  /**
   * @brief Block and drain packets to `conn` until `stop()` is called (6.3.2).
   *
   * Intended to run on a dedicated `std::jthread`.  Waits on a condition
   * variable when the queue is empty.
   *
   * @param conn Connection to write to.
   */
  void run_drain(TcpConnection &conn);

  /**
   * @brief Signal the `run_drain` loop to exit (6.3.2).
   *
   * Safe to call from any thread.  No-op if already stopped.
   */
  void stop();

  /**
   * @brief Return whether the queue is at capacity (6.3.3).
   * @return `true` when queued bytes ≥ max_bytes.
   */
  [[nodiscard]] bool is_full() const noexcept;

  /**
   * @brief Return whether the queue holds no packets.
   * @return `true` when the queue is empty.
   */
  [[nodiscard]] bool is_empty() const noexcept;

  /**
   * @brief Return the current total byte count of all queued packets.
   * @return Byte count.
   */
  [[nodiscard]] std::size_t queued_bytes() const noexcept;

private:
  const std::size_t max_bytes_;            ///< Upper bound for queued_bytes_.
  mutable std::mutex mutex_;               ///< Guards queue_ and queued_bytes_.
  std::condition_variable cv_;             ///< Notified by enqueue() / stop().
  std::queue<std::vector<uint8_t>> queue_; ///< Pending outgoing packets.
  std::size_t queued_bytes_{0};      ///< Running sum of queued packet sizes.
  std::atomic<bool> stopped_{false}; ///< True after stop() is called.
  StructuredTracer *structured_tracer_{nullptr}; ///< Optional diagnostics tracer.
  std::string queue_id_; ///< Trace correlation ID for this queue.
};

} // namespace mqtt
