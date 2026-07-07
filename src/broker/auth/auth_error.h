#pragma once

/**
 * @file auth_error.h
 * @brief AuthError enum and AuthException for the Authentication Module (Module
 * 8).
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mqtt {

/**
 * @brief Error codes for authentication violations in Module 8.
 */
enum class AuthError : uint8_t {
  NotAuthorized, ///< Credentials are missing or do not match.
  BadMethod,     ///< Authentication Method is unsupported or mismatched.
  ProtocolError, ///< AUTH packet received in an unexpected context.
  InvalidState,  ///< Operation attempted in an incompatible handler state.
};

/**
 * @brief Exception thrown by Authentication Module components on protocol or
 * state violations.
 *
 * Derives from `std::runtime_error`. Callers use `error()` to branch on the
 * specific failure code without parsing the human-readable message.
 */
class AuthException : public std::runtime_error {
public:
  /**
   * @brief Construct an AuthException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description.
   */
  explicit AuthException(AuthError err, std::string_view msg)
      : std::runtime_error(std::string(msg)), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The `AuthError` that caused this exception.
   */
  [[nodiscard]] AuthError error() const noexcept { return error_; }

private:
  AuthError error_; ///< Error code stored for programmatic inspection.
};

} // namespace mqtt
