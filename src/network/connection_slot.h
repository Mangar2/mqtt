#pragma once

/**
 * @file connection_slot.h
 * @brief Per-connection non-blocking I/O state holder.
 */

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <span>
#include <vector>

#include "network/tcp_connection.h"

namespace mqtt {

/**
 * @brief Lifecycle phase of a connection slot.
 */
enum class ConnectionPhase : std::uint8_t {
  Connecting,
  Connected,
  Closing
};

/**
 * @brief Per-connection I/O state: socket handle, write ring buffer,
 * and connection phase.
 *
 * Thread safety: none. Access must be externally serialized.
 */
class ConnectionSlot {
public:
  static constexpr std::size_t k_default_write_capacity = 256U * 1024U;
  static constexpr std::size_t k_min_write_capacity = 16U * 1024U;
  static constexpr auto k_write_peak_window = std::chrono::seconds(10);
  static constexpr auto k_write_shrink_idle = std::chrono::seconds(10);

  /**
   * @brief Construct with owned socket handle and ring-buffer capacities.
   * @param socket_handle Connected socket handle.
   * @param write_capacity_bytes Write-buffer capacity.
   */
  explicit ConnectionSlot(
      SocketHandle socket_handle,
      std::size_t write_capacity_bytes = k_default_write_capacity);

  ConnectionSlot(const ConnectionSlot &) = delete;
  ConnectionSlot &operator=(const ConnectionSlot &) = delete;
  ConnectionSlot(ConnectionSlot &&other) noexcept;
  ConnectionSlot &operator=(ConnectionSlot &&other) noexcept;

  [[nodiscard]] SocketHandle fd() const noexcept;
  [[nodiscard]] ConnectionPhase phase() const noexcept;

  /**
   * @brief Transition to a new phase if transition is legal.
   * @param next_phase Target phase.
   * @return True if accepted, false if rejected.
   */
  [[nodiscard]] bool transition_to(ConnectionPhase next_phase) noexcept;

  [[nodiscard]] std::size_t write_size() const noexcept;
  [[nodiscard]] std::size_t write_capacity() const noexcept;
  [[nodiscard]] std::size_t write_free_space() const noexcept;
  [[nodiscard]] bool push_write_bytes(std::span<const uint8_t> data) noexcept;
  [[nodiscard]] std::size_t pop_write_bytes(std::size_t bytes_to_pop) noexcept;
  [[nodiscard]] std::span<const uint8_t> write_contiguous_bytes() const noexcept;

  /**
   * @brief Shrink write buffer capacity after sustained low usage.
   * @param now Current monotonic time.
   */
  void maybe_trim_write_capacity(std::chrono::steady_clock::time_point now) noexcept;

private:
  [[nodiscard]] static bool push_bytes(std::vector<uint8_t> &storage,
                                       std::size_t &head_index,
                                       std::size_t &used_size,
                                       std::span<const uint8_t> data) noexcept;
  [[nodiscard]] static std::size_t pop_bytes(std::size_t &head_index,
                                             std::size_t &used_size,
                                             std::size_t bytes_to_pop,
                                             std::size_t capacity) noexcept;
  [[nodiscard]] static std::span<const uint8_t>
  contiguous_bytes(const std::vector<uint8_t> &storage, std::size_t head_index,
                   std::size_t used_size) noexcept;
  [[nodiscard]] bool ensure_write_capacity_for(std::size_t additional_bytes) noexcept;
  void reallocate_write_storage(std::size_t new_capacity) noexcept;
  void refresh_write_peak_window(std::chrono::steady_clock::time_point now) noexcept;

  SocketHandle socket_handle_{k_invalid_socket};
  ConnectionPhase phase_{ConnectionPhase::Connecting};

  std::vector<uint8_t> write_storage_;
  std::size_t write_limit_capacity_{0};
  std::size_t write_head_index_{0};
  std::size_t write_used_size_{0};
  std::size_t write_peak_window_bytes_{0};
  std::chrono::steady_clock::time_point write_peak_window_started_at_{};
  std::chrono::steady_clock::time_point last_write_activity_at_{};
};

} // namespace mqtt

