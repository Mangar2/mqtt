#pragma once

/**
 * @file write_queue.h
 * @brief WriteQueue — thread-safe outgoing packet queue with async drain
 * (Module 6.3).
 */

#include <atomic>
#include <cstddef>
#include <functional>
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
 * - `enqueue()` pushes a packet and optionally flushes through a configured
 *   sink writer (6.3.1).
 * - `drain()` writes queued packets to the provided `TcpConnection` and
 *   returns to caller (6.3.2).
 * - Backpressure (6.3.3): `enqueue()` returns `false` — without throwing —
 *   when the total queued byte count would exceed `max_bytes`.
 *
 * ### Intended usage
 * Use `set_sink()` to provide a writer callback when immediate flush behavior
 * is desired.
 *
 * Thread safety: `enqueue()`, `stop()`, and `set_sink()` are safe to call
 * concurrently from different threads.  `drain()` is a one-shot synchronous
 * helper intended for single-threaded use or testing.
 */
class WriteQueue {
public:
  using SinkWriter = std::function<bool(std::span<const uint8_t>)>;

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
   * @brief Attach an optional sink writer used for immediate flush on enqueue.
   *
   * When set, `enqueue()` will try to flush pending frames through the sink.
   * The sink executes outside the queue mutex.
   *
   * @param sink_writer Writer callback; empty to disable sink flushing.
   */
  void set_sink(SinkWriter sink_writer) noexcept;

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
  * @brief Signal queue shutdown and reject future enqueue calls.
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
  std::queue<std::vector<uint8_t>> queue_; ///< Pending outgoing packets.
  std::size_t queued_bytes_{0};      ///< Running sum of queued packet sizes.
  std::atomic<bool> stopped_{false}; ///< True after stop() is called.
  StructuredTracer *structured_tracer_{nullptr}; ///< Optional diagnostics tracer.
  std::string queue_id_; ///< Trace correlation ID for this queue.
  SinkWriter sink_writer_; ///< Optional immediate sink writer.
};

} // namespace mqtt
