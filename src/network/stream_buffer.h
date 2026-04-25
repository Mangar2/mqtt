#pragma once

/**
 * @file stream_buffer.h
 * @brief StreamBuffer — accumulates TCP bytes and extracts complete MQTT
 * packets (Module 6.2).
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mqtt {

/**
 * @brief Runtime configuration for `StreamBuffer`.
 */
struct StreamBufferConfig {
  /// Default bytes per chunk.
  static constexpr std::size_t k_default_chunk_size = 16U * 1024U;
  /// Default hard cap for buffered bytes per connection.
  static constexpr std::size_t k_default_max_buffered = 1U * 1024U * 1024U;
  /// Default count of drained chunks retained for reuse.
  static constexpr std::size_t k_default_free_list_max = 4U;

  /// Bytes in each fixed-size chunk.
  std::size_t chunk_size = k_default_chunk_size;
  /// Hard cap of currently buffered unread bytes.
  std::size_t max_buffered = k_default_max_buffered;
  /// Max chunks kept in free list before release to allocator.
  std::size_t free_list_max = k_default_free_list_max;
};

/**
 * @brief Result of `StreamBuffer::append()`.
 */
enum class StreamBufferAppendResult : uint8_t {
  kOk,
  kBufferFull,
};

/**
 * @brief Accumulates a raw TCP byte stream and extracts complete MQTT packets
 * (Module 6.2).
 *
 * MQTT frames are detected using the fixed-header Remaining Length field
 * (a variable-byte-integer at byte offset 1–4 of every packet):
 *
 * - Byte 0: packet type + flags.
 * - Bytes 1–4: Remaining Length (VBI).  The MSB of each byte is 1 if more
 *   bytes follow; 0 on the last byte.  At most 4 bytes are used.
 * - Total packet size = (1 + encoded_rl_size) + remaining_length.
 *
 * Incomplete data is retained across calls to `append()` until the next
 * fragment arrives (6.2.3).
 *
 * ### Usage
 * ```cpp
 * StreamBuffer buf;
 * while (true) {
 *     std::array<uint8_t, 4096> chunk{};
 *     ssize_t n = conn.read(chunk);
 *     buf.append(std::span{chunk}.first(n));
 *     while (buf.has_complete_packet()) {
 *         auto pkt = buf.consume_packet();
 *         // hand pkt to the codec …
 *     }
 * }
 * ```
 *
 * Thread safety: none — external synchronisation required.
 */
class StreamBuffer {
public:
  /**
   * @brief Construct a stream buffer with fixed-size chunk storage.
   * @param config Runtime limits and chunk sizing.
   * @throws std::invalid_argument if `chunk_size < 16` or `max_buffered == 0`.
   */
  explicit StreamBuffer(StreamBufferConfig config = {});

  ~StreamBuffer();

  StreamBuffer(const StreamBuffer &) = delete;
  StreamBuffer &operator=(const StreamBuffer &) = delete;
  StreamBuffer(StreamBuffer &&other) noexcept;
  StreamBuffer &operator=(StreamBuffer &&other) noexcept;

  /**
   * @brief Append bytes received from the socket to the internal buffer
   * (6.2.1).
   * @param data Byte span (may be empty — no-op).
   * @return `kBufferFull` when the append would exceed configured hard cap.
   */
  StreamBufferAppendResult append(std::span<const uint8_t> data) noexcept;

  /**
   * @brief Return whether at least one complete MQTT packet is buffered
   * (6.2.2).
   * @return `true` if `consume_packet()` would succeed.
   */
  [[nodiscard]] bool has_complete_packet() const noexcept;

  /**
   * @brief Remove and return the next complete MQTT packet from the front of
   * the buffer (6.2.3).
   *
   * The returned vector contains the full wire representation including the
   * fixed header.
   *
   * @pre `has_complete_packet()` must be `true`.
   * @return Complete packet bytes.
   */
  [[nodiscard]] std::vector<uint8_t> consume_packet();

  /**
   * @brief Return whether the internal byte buffer holds no bytes.
   * @return `true` when empty.
   */
  [[nodiscard]] bool is_empty() const noexcept;

  [[nodiscard]] std::size_t size() const noexcept;

  /**
   * @brief Return bytes currently allocated in chunk storage.
   * @return Sum of in-use and free-list chunk capacities.
   */
  [[nodiscard]] std::size_t capacity() const noexcept;

private:
  struct Chunk {
    explicit Chunk(std::size_t chunk_size);

    Chunk(const Chunk &) = delete;
    Chunk &operator=(const Chunk &) = delete;

    std::vector<uint8_t> data;
    std::size_t read_pos{0U};
    std::size_t write_pos{0U};
    Chunk *next{nullptr};
  };

  /**
   * @brief Compute the total wire size of the front packet.
   *
   * Parses the VBI Remaining Length field starting at offset 1.
   *
   * @return Total byte count of the packet, or `std::nullopt` if not enough
   *         bytes are available yet to determine the size.
   */
  [[nodiscard]] std::optional<std::size_t> front_packet_size() const noexcept;

  [[nodiscard]] Chunk *acquire_chunk();
  void release_chunk(Chunk *chunk) noexcept;
  void clear_all_chunks() noexcept;
  [[nodiscard]] std::size_t peek_bytes(uint8_t *out,
                                       std::size_t max_count) const noexcept;

  StreamBufferConfig config_;
  Chunk *head_{nullptr};
  Chunk *tail_{nullptr};
  Chunk *free_head_{nullptr};
  std::size_t free_count_{0U};
  std::size_t allocated_chunk_count_{0U};
  std::size_t size_{0U};
};

} // namespace mqtt
