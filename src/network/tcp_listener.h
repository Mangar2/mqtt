#pragma once

/**
 * @file tcp_listener.h
 * @brief TcpListener — opens a server socket and accepts TCP connections
 * (Module 6.1.1–6.1.2).
 */

#include <cstdint>
#include <memory>

#include "network/tcp_connection.h"

namespace mqtt {

/**
 * @brief Opens a TCP server socket and provides a blocking accept loop
 * (Module 6.1).
 *
 * `TcpListener::listen()` creates and binds the socket; subsequent calls to
 * `accept()` block until a client connects and return a newly-owned
 * `TcpConnection`.
 *
 * ### IPv4 / IPv6 (6.1.1)
 * When `ipv6 = false` an `AF_INET` socket is created.  When `ipv6 = true` an
 * `AF_INET6` socket is created with `IPV6_V6ONLY = 0`, making it dual-stack
 * so that both IPv4 and IPv6 clients can connect.
 *
 * ### Socket options
 * `SO_REUSEADDR` is always set so that a restarted broker can bind the same
 * port without waiting for `TIME_WAIT` to expire.
 *
 * ### Thread safety
 * Not thread-safe.  Intended to run its accept loop on a single dedicated
 * thread.
 */
class TcpListener {
public:
  /**
   * @brief Open a server socket bound to `port` (6.1.1).
   *
   * Creates the socket, applies `SO_REUSEADDR`, binds, and calls `listen()`.
   *
   * @param port TCP port number to bind.
   * @param ipv6 `true` to use AF_INET6 (dual-stack); `false` for AF_INET.
   * @return A ready-to-accept `TcpListener`.
   * @throws NetworkException on any socket/bind/listen failure.
   */
  [[nodiscard]] static TcpListener listen(uint16_t port, bool ipv6 = false);

  /**
   * @brief Block until a client connects and return the new connection
   * (6.1.2–6.1.3).
   *
   * @return Heap-allocated `TcpConnection` owning the accepted socket fd.
   * @throws NetworkException(AcceptFailed) when `accept()` returns an error.
   */
  [[nodiscard]] std::unique_ptr<TcpConnection> accept() const;

  /**
   * @brief Close the server socket.
   *
   * Any blocked `accept()` call will receive an error after `close()`.
   * No-op when already closed.
   */
  void close() noexcept;

  /**
   * @brief Return whether the server socket is open.
   * @return `true` while the fd is valid.
   */
  [[nodiscard]] bool is_open() const noexcept;

  /**
   * @brief Return the port this listener is bound to.
   *
   * Useful when the socket was bound to port 0 (OS-assigned port).
   *
   * @return Bound TCP port in host byte order, or 0 on error.
   */
  [[nodiscard]] uint16_t port() const noexcept;

  /** @brief Close the server socket on destruction if still open. */
  ~TcpListener();

  TcpListener(const TcpListener &) = delete;
  TcpListener &operator=(const TcpListener &) = delete;

  /** @brief Move constructor — transfers fd ownership. */
  TcpListener(TcpListener &&other) noexcept;

  /** @brief Move assignment — transfers fd ownership. */
  TcpListener &operator=(TcpListener &&other) noexcept;

private:
  explicit TcpListener(SocketHandle hdl) noexcept;

  SocketHandle fd_{k_invalid_socket}; ///< Owned server socket handle;
                                      ///< k_invalid_socket when closed.
};

} // namespace mqtt
