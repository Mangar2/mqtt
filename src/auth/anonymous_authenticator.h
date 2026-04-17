#pragma once

/**
 * @file anonymous_authenticator.h
 * @brief AnonymousAuthenticator — policy-driven allow/deny without credentials
 * (Module 8.4).
 */

#include "auth/authenticator.h"
#include <cstdint>


namespace mqtt {

/**
 * @brief Policy that governs anonymous access.
 */
enum class AnonymousPolicy : uint8_t {
  Allow, ///< Any connection is accepted regardless of credentials.
  Deny,  ///< All connections are rejected with NotAuthorized.
};

/**
 * @brief Authenticator that allows or denies all connections without
 * inspecting credentials (Module 8.4.1).
 *
 * Useful as a permissive default during development or as a placeholder in
 * pipelines where authentication is handled out-of-band.
 *
 * Thread safety: none — external synchronisation required.
 */
class AnonymousAuthenticator final : public IAuthenticator {
public:
  /**
   * @brief Construct with the given access policy.
   * @param policy `Allow` to accept all connections; `Deny` to reject all.
   */
  explicit AnonymousAuthenticator(AnonymousPolicy policy) noexcept;

  /**
   * @brief Accept or reject the connection based on the configured policy.
   *
   * Credentials present in `connect` are ignored.
   *
   * @param connect Decoded CONNECT packet (unused).
   * @return `Success` when policy is `Allow`; `Failure / NotAuthorized` when
   *         policy is `Deny`.
   */
  AuthResult authenticate(const ConnectPacket &connect) override;

  /**
   * @brief Return the configured policy.
   * @return Current `AnonymousPolicy`.
   */
  [[nodiscard]] AnonymousPolicy policy() const noexcept;

private:
  AnonymousPolicy policy_; ///< Configured access policy.
};

} // namespace mqtt
