// This file is POSIX-only; excluded on Windows via CMake.
// The guard also silences IntelliSense on Windows.
#if !defined(_WIN32)

#include "network/tcp_listener.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "network/network_error.h"
#include "network/tcp_connection.h"

namespace mqtt {

namespace {

/**
 * @brief Format a POSIX errno-based error message.
 * @param prefix Short label of the failing call.
 * @return Combined message string.
 */
[[nodiscard]] std::string make_error_msg(const char *prefix) {
  return std::string(prefix) + ": " + std::strerror(errno);
}

/** @brief Throw a formatted NetworkException for a POSIX call failure. */
[[noreturn]] void throw_network_error(NetworkError code, const char *prefix) {
  throw NetworkException(code, make_error_msg(prefix));
}

/**
 * @brief Close descriptor and throw formatted NetworkException.
 *
 * @param sfd Socket descriptor to close before throwing.
 * @param code Error code to emit.
 * @param prefix POSIX call label used in the message.
 */
[[noreturn]] void close_and_throw(int sfd, NetworkError code,
                                  const char *prefix) {
  ::close(sfd);
  throw_network_error(code, prefix);
}

/** @brief Cast a SocketHandle to a POSIX file descriptor. */
[[nodiscard]] int to_fd(SocketHandle hdl) noexcept {
  return static_cast<int>(hdl);
}

} // namespace

TcpListener::TcpListener(SocketHandle hdl) noexcept : fd_(hdl) {}

TcpListener::~TcpListener() { close(); }

TcpListener::TcpListener(TcpListener &&other) noexcept : fd_(other.fd_) {
  other.fd_ = k_invalid_socket;
}

TcpListener &TcpListener::operator=(TcpListener &&other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = k_invalid_socket;
  }
  return *this;
}

TcpListener TcpListener::listen(uint16_t port, bool ipv6) {
  int domain = ipv6 ? AF_INET6 : AF_INET;
  int sfd = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (sfd < 0) {
    throw_network_error(NetworkError::SocketCreateFailed, "socket()");
  }

  int opt = 1;
  if (::setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    close_and_throw(sfd, NetworkError::SetSockOptFailed,
                    "setsockopt(SO_REUSEADDR)");
  }

  if (ipv6) {
    int v6only = 0;
    if (::setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) <
        0) {
      close_and_throw(sfd, NetworkError::SetSockOptFailed,
                      "setsockopt(IPV6_V6ONLY)");
    }
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;
    if (::bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      close_and_throw(sfd, NetworkError::BindFailed, "bind()");
    }
  } else {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
      close_and_throw(sfd, NetworkError::BindFailed, "bind()");
    }
  }

  constexpr int k_backlog = 128;
  if (::listen(sfd, k_backlog) < 0) {
    close_and_throw(sfd, NetworkError::ListenFailed, "listen()");
  }
  return TcpListener{static_cast<SocketHandle>(sfd)};
}

std::unique_ptr<TcpConnection> TcpListener::accept() const {
  int conn_fd = ::accept(to_fd(fd_), nullptr, nullptr);
  if (conn_fd < 0) {
    throw NetworkException(NetworkError::AcceptFailed,
                           make_error_msg("accept()"));
  }
  return std::make_unique<TcpConnection>(static_cast<SocketHandle>(conn_fd));
}

void TcpListener::close() noexcept {
  if (fd_ != k_invalid_socket) {
    ::shutdown(to_fd(fd_), SHUT_RDWR);
    ::close(to_fd(fd_));
    fd_ = k_invalid_socket;
  }
}

bool TcpListener::is_open() const noexcept { return fd_ != k_invalid_socket; }

uint16_t TcpListener::port() const noexcept {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  if (::getsockname(to_fd(fd_), reinterpret_cast<sockaddr *>(&addr), &len) <
      0) {
    return 0;
  }
  if (addr.ss_family == AF_INET6) {
    return ntohs(reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_port);
  }
  return ntohs(reinterpret_cast<sockaddr_in *>(&addr)->sin_port);
}

} // namespace mqtt

#endif // !defined(_WIN32)
