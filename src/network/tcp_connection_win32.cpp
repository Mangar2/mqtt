#include "network/tcp_connection.h"

#include <winsock2.h>

#include <cstdint>
#include <span>

namespace mqtt {

namespace {

/** @brief Cast a platform-neutral SocketHandle to a Windows SOCKET. */
[[nodiscard]] SOCKET to_socket(SocketHandle hdl) noexcept {
  return static_cast<SOCKET>(hdl);
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
  int result = ::recv(to_socket(fd_), reinterpret_cast<char *>(buf.data()),
                      static_cast<int>(buf.size()), 0);
  return static_cast<std::ptrdiff_t>(result);
}

bool TcpConnection::write(std::span<const uint8_t> buf) const noexcept {
  std::size_t sent = 0;
  while (sent < buf.size()) {
    int result = ::send(to_socket(fd_),
                        reinterpret_cast<const char *>(buf.data() + sent),
                        static_cast<int>(buf.size() - sent), 0);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

void TcpConnection::close() noexcept {
  if (fd_ != k_invalid_socket) {
    ::shutdown(to_socket(fd_), SD_BOTH);
    ::closesocket(to_socket(fd_));
    fd_ = k_invalid_socket;
  }
}

bool TcpConnection::is_open() const noexcept { return fd_ != k_invalid_socket; }

SocketHandle TcpConnection::fd() const noexcept { return fd_; }

} // namespace mqtt
