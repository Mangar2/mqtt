// This file is POSIX-only; excluded on Windows via CMake.
// The guard also silences IntelliSense on Windows.
#if !defined(_WIN32)

#include "network/tcp_connection.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdint>
#include <span>

namespace mqtt {

namespace {

/** @brief Cast a platform-neutral SocketHandle to a POSIX file descriptor. */
[[nodiscard]] int to_fd(SocketHandle hdl) noexcept {
  return static_cast<int>(hdl);
}

} // namespace

TcpConnection::TcpConnection(SocketHandle hdl) noexcept : fd_(hdl) {}

TcpConnection::~TcpConnection() { close(); }

TcpConnection::TcpConnection(TcpConnection &&other) noexcept : fd_(other.fd_) {
  other.fd_ = k_invalid_socket;
}

TcpConnection &TcpConnection::operator=(TcpConnection &&other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    other.fd_ = k_invalid_socket;
  }
  return *this;
}

std::ptrdiff_t TcpConnection::read(std::span<uint8_t> buf) const noexcept {
  last_timed_out_ = false;
  std::ptrdiff_t result = ::recv(to_fd(fd_), buf.data(), buf.size(), 0);
  if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    last_timed_out_ = true;
  }
  return result;
}

void TcpConnection::set_receive_timeout(uint32_t milliseconds_val) noexcept {
  struct timeval tv{};
  tv.tv_sec = static_cast<time_t>(milliseconds_val / 1000U);
  tv.tv_usec = static_cast<suseconds_t>((milliseconds_val % 1000U) * 1000U);
  ::setsockopt(to_fd(fd_), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const void *>(&tv), sizeof(tv));
}

bool TcpConnection::last_read_timed_out() const noexcept {
  return last_timed_out_;
}

bool TcpConnection::write(std::span<const uint8_t> buf) const noexcept {
  std::size_t sent = 0;
  while (sent < buf.size()) {
    std::ptrdiff_t result =
        ::send(to_fd(fd_), buf.data() + sent, buf.size() - sent, MSG_NOSIGNAL);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

void TcpConnection::close() noexcept {
  if (fd_ != k_invalid_socket) {
    ::shutdown(to_fd(fd_), SHUT_RDWR);
    ::close(to_fd(fd_));
    fd_ = k_invalid_socket;
  }
}

bool TcpConnection::is_open() const noexcept { return fd_ != k_invalid_socket; }

SocketHandle TcpConnection::fd() const noexcept { return fd_; }

} // namespace mqtt

#endif // !defined(_WIN32)
