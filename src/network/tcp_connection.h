#pragma once

/**
 * @file tcp_connection.h
 * @brief TcpConnection — owns a connected TCP socket file descriptor
 * (Module 6.1.3).
 */

#include <cstddef>
#include <cstdint>
#include <span>

namespace mqtt {

/**
 * @brief Platform-neutral socket handle type.
 *
 * Defined as `uintptr_t` so it can hold both a POSIX `int` file descriptor
 * and a Windows `SOCKET` (`UINT_PTR`) without including any platform SDK
 * header in this file.
 */
using SocketHandle = uintptr_t;

/**
 * @brief Sentinel value for an invalid or closed socket.
 *
 * Equals `~uintptr_t{0}` (all bits set), which corresponds to both
 * `INVALID_SOCKET` on Windows and `(uintptr_t)(-1)` on POSIX.
 */
inline constexpr SocketHandle k_invalid_socket = ~uintptr_t{0};

/**
 * @brief Owns a single connected TCP socket.
 *
 * Wraps a socket handle obtained from `accept()` (server side) or an OS
 * connect call (client side).  Provides blocking read and write operations.
 *
 * ### Ownership
 * Exclusive ownership of the handle.  Moving transfers ownership; copying is
 * disabled.  The destructor closes the handle if still open.
 *
 * ### Thread safety
 * Not thread-safe.  External synchronisation required for concurrent use.
 */
class TcpConnection {
public:
  /**
   * @brief Take ownership of an already-connected socket handle.
   * @param hdl Valid connected socket handle.
   */
  explicit TcpConnection(SocketHandle hdl) noexcept;

  /** @brief Close the socket on destruction if still open. */
  ~TcpConnection();

  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;

  /** @brief Move constructor — transfers handle ownership. */
  TcpConnection(TcpConnection &&other) noexcept;

  /** @brief Move assignment — transfers handle ownership. */
  TcpConnection &operator=(TcpConnection &&other) noexcept;

  /**
   * @brief Blocking receive into `buf`.
   *
   * @param buf Destination span; must not be empty.
   * @return Number of bytes read (≥ 1), 0 when the peer closed the
   *         connection, or -1 on a socket error.
   */
  [[nodiscard]] std::ptrdiff_t read(std::span<uint8_t> buf) const noexcept;

  /**
   * @brief Blocking send of all bytes in `buf`.
   *
   * Loops until every byte has been sent or a socket error occurs.
   *
   * @param buf Data to transmit.
   * @return `true` on full success; `false` on any socket error.
   */
  [[nodiscard]] bool write(std::span<const uint8_t> buf) const noexcept;

  /**
   * @brief Shutdown and close the socket.
   *
   * No-op when already closed.
   */
  void close() noexcept;

  /**
   * @brief Return whether the socket is currently open.
   * @return `true` while the handle is valid and `close()` has not been called.
   */
  [[nodiscard]] bool is_open() const noexcept;

  /**
   * @brief Return the underlying socket handle.
   * @return The handle, or `k_invalid_socket` when closed.
   */
  [[nodiscard]] SocketHandle fd() const noexcept;

private:
  SocketHandle fd_{k_invalid_socket}; ///< Owned socket handle.
};

} // namespace mqtt
