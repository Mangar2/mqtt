#pragma once

/**
 * @file network_error.h
 * @brief NetworkError enum and NetworkException for the Network Layer (Module
 * 6).
 */

#include <stdexcept>
#include <string>
#include <string_view>

namespace mqtt {

/**
 * @brief Error codes for socket and I/O failures in the Network Layer.
 */
enum class NetworkError : std::uint8_t {
  SocketCreateFailed, ///< socket() system call failed.
  BindFailed,         ///< bind() system call failed.
  ListenFailed,       ///< listen() system call failed.
  AcceptFailed,       ///< accept() system call failed.
  SetSockOptFailed,   ///< setsockopt() system call failed.
  WriteFailed,        ///< Send to socket failed.
  ReadFailed,         ///< Receive from socket failed.
  QueueFull,          ///< Write queue capacity exceeded (backpressure signal).
};

/**
 * @brief Exception type thrown by Network Layer components on fatal errors.
 */
class NetworkException : public std::runtime_error {
public:
  /**
   * @brief Construct with a specific error code and message.
   * @param code    The network error code that caused this exception.
   * @param message Human-readable description of the failure.
   */
  NetworkException(NetworkError code, std::string_view message)
      : std::runtime_error(std::string(message)), code_(code) {}

  /**
   * @brief Return the error code that caused this exception.
   * @return The NetworkError value.
   */
  [[nodiscard]] NetworkError code() const noexcept { return code_; }

private:
  NetworkError code_; ///< The specific error that triggered this exception.
};

} // namespace mqtt
