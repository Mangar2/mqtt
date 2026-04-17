#include "network/tcp_listener.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>

#include "network/network_error.h"
#include "network/tcp_connection.h"

namespace mqtt {

namespace {

/**
 * @brief Initialize Winsock lazily on first call.
 *
 * Uses a function-local static so there is no global destructor.
 * WSACleanup is intentionally omitted: the OS reclaims all WSA resources on
 * process exit.
 */
void ensure_wsa_initialized() noexcept {
  static const bool initialized = []() noexcept {
    WSADATA wsa{};
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
    return true;
  }();
  (void)initialized;
}

/**
 * @brief Format a Winsock error message.
 * @param prefix Short label of the failing call.
 * @return Combined message string.
 */
[[nodiscard]] std::string make_error_msg(const char *prefix) {
  char buf[256]{};
  strerror_s(buf, sizeof(buf), errno);
  return std::string(prefix) + ": " + buf;
}

/** @brief Cast a SocketHandle to a Windows SOCKET. */
[[nodiscard]] SOCKET to_socket(SocketHandle hdl) noexcept {
  return static_cast<SOCKET>(hdl);
}

/**
 * @brief Close @p sfd and throw NetworkException.
 *
 * Single-line call sites keep the error paths to one uncovered line each
 * when the failure branch is not exercised by tests.
 */
[[noreturn]] void close_and_fail(SOCKET sfd, NetworkError code,
                                 const char *msg) {
  ::closesocket(sfd);
  throw NetworkException(code, make_error_msg(msg));
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
  ensure_wsa_initialized();

  int    domain = ipv6 ? AF_INET6 : AF_INET;
  SOCKET sfd    = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (sfd == INVALID_SOCKET)
    throw NetworkException(NetworkError::SocketCreateFailed,
                           make_error_msg("socket()"));

  BOOL opt = TRUE;
  if (::setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&opt),
                   sizeof(opt)) == SOCKET_ERROR)
    close_and_fail(sfd, NetworkError::SetSockOptFailed,
                   "setsockopt(SO_REUSEADDR)");

  if (ipv6) {
    DWORD v6only = 0;
    if (::setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY,
                     reinterpret_cast<const char *>(&v6only),
                     sizeof(v6only)) == SOCKET_ERROR)
      close_and_fail(sfd, NetworkError::SetSockOptFailed,
                     "setsockopt(IPV6_V6ONLY)");
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port   = htons(port);
    addr.sin6_addr   = in6addr_any;
    if (::bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR)
      close_and_fail(sfd, NetworkError::BindFailed, "bind()");
  } else {
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR)
      close_and_fail(sfd, NetworkError::BindFailed, "bind()");
  }

  constexpr int k_backlog = 128;
  if (::listen(sfd, k_backlog) == SOCKET_ERROR)
    close_and_fail(sfd, NetworkError::ListenFailed, "listen()");
  return TcpListener{static_cast<SocketHandle>(sfd)};
}

std::unique_ptr<TcpConnection> TcpListener::accept() {
  SOCKET conn_fd = ::accept(to_socket(fd_), nullptr, nullptr);
  if (conn_fd == INVALID_SOCKET) {
    throw NetworkException(NetworkError::AcceptFailed,
                           make_error_msg("accept()"));
  }
  return std::make_unique<TcpConnection>(static_cast<SocketHandle>(conn_fd));
}

void TcpListener::close() noexcept {
  if (fd_ != k_invalid_socket) {
    ::shutdown(to_socket(fd_), SD_BOTH);
    ::closesocket(to_socket(fd_));
    fd_ = k_invalid_socket;
  }
}

bool TcpListener::is_open() const noexcept { return fd_ != k_invalid_socket; }

uint16_t TcpListener::port() const noexcept {
  sockaddr_storage addr{};
  int len = sizeof(addr);
  if (::getsockname(to_socket(fd_), reinterpret_cast<sockaddr *>(&addr),
                    &len) == SOCKET_ERROR) {
    return 0;
  }
  if (addr.ss_family == AF_INET6) {
    return ntohs(reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_port);
  }
  return ntohs(reinterpret_cast<sockaddr_in *>(&addr)->sin_port);
}

} // namespace mqtt
