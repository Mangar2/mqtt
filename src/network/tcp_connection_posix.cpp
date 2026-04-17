#include "network/tcp_connection.h"

#include <sys/socket.h>
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

std::ptrdiff_t TcpConnection::read(std::span<uint8_t> buf) noexcept {
  return ::recv(to_fd(fd_), buf.data(), buf.size(), 0);
}

bool TcpConnection::write(std::span<const uint8_t> buf) noexcept {
  std::size_t sent = 0;
  while (sent < buf.size()) {
    ssize_t result =
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
