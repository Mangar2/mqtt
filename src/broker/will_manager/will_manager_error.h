#pragma once

/**
 * @file will_manager_error.h
 * @brief Error types for the Will Manager module (Module 11).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes for the Will Manager module.
 */
enum class WillManagerError : uint8_t {
  ClientIdNotFound, ///< Operation referenced a Client ID with no stored will.
};

/**
 * @brief Exception thrown by the Will Manager on unrecoverable violations.
 *
 * Derives from `std::runtime_error`.
 */
class WillManagerException : public std::runtime_error {
public:
  /**
   * @brief Construct a WillManagerException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description of the error.
   */
  explicit WillManagerException(WillManagerError err, const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The WillManagerError that caused this exception.
   */
  [[nodiscard]] WillManagerError error() const noexcept { return error_; }

private:
  WillManagerError error_; ///< Stored error code.
};

} // namespace mqtt
