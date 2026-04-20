#pragma once

/**
 * @file socket_ops.h
 * @brief Non-blocking socket helper functions for reactor-style I/O.
 */

#include <cstddef>
#include <cstdint>
#include <span>

#include "network/tcp_connection.h"

namespace mqtt {

/**
 * @brief Result category for non-blocking socket operations.
 */
enum class IoResult : std::uint8_t {
  Ok,         ///< Operation completed successfully.
  WouldBlock, ///< Socket has no data/capacity right now (EAGAIN/EWOULDBLOCK).
  Closed,     ///< Peer closed the connection.
  Error       ///< Fatal socket error.
};

/**
 * @brief Set a socket handle to non-blocking mode.
 * @param socket_handle Socket handle.
 * @return IoResult::Ok on success, IoResult::Error on failure.
 */
[[nodiscard]] IoResult set_nonblocking(SocketHandle socket_handle) noexcept;

/**
 * @brief Perform one non-blocking recv() call.
 * @param socket_handle Connected socket handle.
 * @param destination Destination byte span.
 * @param bytes_read Optional out parameter for bytes read on IoResult::Ok.
 * @return IoResult for the operation outcome.
 */
[[nodiscard]] IoResult nb_read(SocketHandle socket_handle,
                               std::span<uint8_t> destination,
                               std::size_t *bytes_read = nullptr) noexcept;

/**
 * @brief Perform one non-blocking send() call.
 * @param socket_handle Connected socket handle.
 * @param source Source byte span.
 * @param bytes_written Optional out parameter for bytes sent on IoResult::Ok.
 * @return IoResult for the operation outcome.
 */
[[nodiscard]] IoResult nb_write(SocketHandle socket_handle,
                                std::span<const uint8_t> source,
                                std::size_t *bytes_written = nullptr) noexcept;

/**
 * @brief Perform one non-blocking accept() call.
 * @param listen_socket_handle Listening socket handle.
 * @param accepted_socket_handle Optional out parameter for accepted socket.
 * @return IoResult for the operation outcome.
 */
[[nodiscard]] IoResult
nb_accept(SocketHandle listen_socket_handle,
          SocketHandle *accepted_socket_handle = nullptr) noexcept;

} // namespace mqtt
