#include "network/socket_ops.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstddef>
#include <cstdint>

namespace mqtt {

namespace {

#ifdef _WIN32
[[nodiscard]] SOCKET to_socket(SocketHandle socket_handle) noexcept {
  return static_cast<SOCKET>(socket_handle);
}

[[nodiscard]] bool is_would_block_error() noexcept {
  const int error_code = WSAGetLastError();
  return error_code == WSAEWOULDBLOCK;
}

void close_socket_handle(SocketHandle socket_handle) noexcept {
  ::closesocket(to_socket(socket_handle));
}
#else
[[nodiscard]] int to_fd(SocketHandle socket_handle) noexcept {
  return static_cast<int>(socket_handle);
}

[[nodiscard]] bool is_would_block_error() noexcept {
  return errno == EAGAIN || errno == EWOULDBLOCK;
}

void close_socket_handle(SocketHandle socket_handle) noexcept {
  ::close(to_fd(socket_handle));
}
#endif

} // namespace

IoResult set_nonblocking(SocketHandle socket_handle) noexcept {
#ifdef _WIN32
  u_long mode = 1;
  if (::ioctlsocket(to_socket(socket_handle), FIONBIO, &mode) == SOCKET_ERROR) {
    return IoResult::Error;
  }
  return IoResult::Ok;
#else
  const int current_flags = ::fcntl(to_fd(socket_handle), F_GETFL, 0);
  if (current_flags < 0) {
    return IoResult::Error;
  }
  if (::fcntl(to_fd(socket_handle), F_SETFL, current_flags | O_NONBLOCK) < 0) {
    return IoResult::Error;
  }
  return IoResult::Ok;
#endif
}

IoResult nb_read(SocketHandle socket_handle, std::span<uint8_t> destination,
                 std::size_t *bytes_read) noexcept {
  if (bytes_read != nullptr) {
    *bytes_read = 0;
  }

#ifdef _WIN32
  const int read_result =
      ::recv(to_socket(socket_handle), reinterpret_cast<char *>(destination.data()),
             static_cast<int>(destination.size()), 0);
#else
  const std::ptrdiff_t read_result =
      ::recv(to_fd(socket_handle), destination.data(), destination.size(), 0);
#endif

  if (read_result > 0) {
    if (bytes_read != nullptr) {
      *bytes_read = static_cast<std::size_t>(read_result);
    }
    return IoResult::Ok;
  }
  if (read_result == 0) {
    return IoResult::Closed;
  }
  return is_would_block_error() ? IoResult::WouldBlock : IoResult::Error;
}

IoResult nb_write(SocketHandle socket_handle, std::span<const uint8_t> source,
                  std::size_t *bytes_written) noexcept {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }

#ifdef _WIN32
  const int write_result =
      ::send(to_socket(socket_handle), reinterpret_cast<const char *>(source.data()),
             static_cast<int>(source.size()), 0);
#else
  const std::ptrdiff_t write_result =
      ::send(to_fd(socket_handle), source.data(), source.size(), MSG_NOSIGNAL);
#endif

  if (write_result > 0) {
    if (bytes_written != nullptr) {
      *bytes_written = static_cast<std::size_t>(write_result);
    }
    return IoResult::Ok;
  }
  if (write_result == 0) {
    return IoResult::Closed;
  }
  return is_would_block_error() ? IoResult::WouldBlock : IoResult::Error;
}

IoResult nb_accept(SocketHandle listen_socket_handle,
                   SocketHandle *accepted_socket_handle) noexcept {
  if (accepted_socket_handle != nullptr) {
    *accepted_socket_handle = k_invalid_socket;
  }

#ifdef _WIN32
  const SOCKET accepted_socket =
      ::accept(to_socket(listen_socket_handle), nullptr, nullptr);
  if (accepted_socket == INVALID_SOCKET) {
    return is_would_block_error() ? IoResult::WouldBlock : IoResult::Error;
  }

  const SocketHandle accepted_handle = static_cast<SocketHandle>(accepted_socket);
  if (set_nonblocking(accepted_handle) != IoResult::Ok) {
    close_socket_handle(accepted_handle);
    return IoResult::Error;
  }
#else
  const int accepted_fd = ::accept(to_fd(listen_socket_handle), nullptr, nullptr);
  if (accepted_fd < 0) {
    return is_would_block_error() ? IoResult::WouldBlock : IoResult::Error;
  }

  const SocketHandle accepted_handle = static_cast<SocketHandle>(accepted_fd);
  if (set_nonblocking(accepted_handle) != IoResult::Ok) {
    close_socket_handle(accepted_handle);
    return IoResult::Error;
  }
#endif

  if (accepted_socket_handle != nullptr) {
    *accepted_socket_handle = accepted_handle;
  }
  return IoResult::Ok;
}

} // namespace mqtt

