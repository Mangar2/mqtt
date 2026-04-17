#pragma once

/**
 * @file authz_error.h
 * @brief AuthzError enum and AuthzException for the Authorization Module
 * (Module 9).
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mqtt {

/**
 * @brief Error codes for authorization violations in Module 9.
 */
enum class AuthzError : uint8_t {
  InvalidAction, ///< Unknown or unsupported action string in ACL config.
  InvalidEffect, ///< Unknown or unsupported effect string in ACL config.
};

/**
 * @brief Exception thrown by Authorization Module components on configuration
 * or rule-evaluation errors.
 *
 * Derives from `std::runtime_error`. Callers use `error()` to branch on the
 * specific failure code without parsing the human-readable message.
 */
class AuthzException : public std::runtime_error {
public:
  /**
   * @brief Construct an AuthzException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description.
   */
  explicit AuthzException(AuthzError err, std::string_view msg)
      : std::runtime_error(std::string(msg)), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The `AuthzError` that caused this exception.
   */
  [[nodiscard]] AuthzError error() const noexcept { return error_; }

private:
  AuthzError error_; ///< Error code stored for programmatic inspection.
};

} // namespace mqtt
