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
#include <vector>

namespace mqtt {

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
   * @brief Append bytes received from the socket to the internal buffer
   * (6.2.1).
   * @param data Byte span (may be empty — no-op).
   */
  void append(std::span<const uint8_t> data);

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

private:
  /**
   * @brief Compute the total wire size of the front packet.
   *
   * Parses the VBI Remaining Length field starting at offset 1.
   *
   * @return Total byte count of the packet, or `std::nullopt` if not enough
   *         bytes are available yet to determine the size.
   */
  [[nodiscard]] std::optional<std::size_t> front_packet_size() const noexcept;

  std::vector<uint8_t> buffer_; ///< Accumulated incoming bytes.
};

} // namespace mqtt
