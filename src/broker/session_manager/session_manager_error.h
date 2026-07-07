#pragma once

/**
 * @file session_manager_error.h
 * @brief Error types for the Session Manager module (Module 10).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes for the Session Manager module.
 */
enum class SessionManagerError : uint8_t {
  InvalidClientId, ///< CONNECT client_id is empty.
};

/**
 * @brief Exception thrown by the Session Manager on unrecoverable violations.
 *
 * Derives from std::runtime_error.
 */
class SessionManagerException : public std::runtime_error {
public:
  /**
   * @brief Construct a SessionManagerException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description of the error.
   */
  explicit SessionManagerException(SessionManagerError err,
                                   const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The SessionManagerError that caused this exception.
   */
  [[nodiscard]] SessionManagerError error() const noexcept { return error_; }

private:
  SessionManagerError error_; ///< Stored error code.
};

} // namespace mqtt
