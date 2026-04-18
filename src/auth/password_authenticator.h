#pragma once

/**
 * @file password_authenticator.h
 * @brief PasswordAuthenticator — username/password credential validation
 * (Module 8.2).
 */

#include "auth/authenticator.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include <unordered_map>

namespace mqtt {

/**
 * @brief Authenticator that validates username/password credentials from a
 * CONNECT packet against an in-memory credential store (Module 8.2).
 *
 * Credentials are stored as `{username → password}` pairs, where the password
 * is raw binary data matching `ConnectPacket::password`.
 *
 * Authentication rules (8.2.2):
 * - `ConnectPacket::username` must be present; otherwise the result is
 *   `Failure / BadUserNameOrPassword`.
 * - The provided password must match the stored entry exactly (binary
 *   comparison); otherwise the result is `Failure / BadUserNameOrPassword`.
 * - Unknown usernames also return `Failure / BadUserNameOrPassword`.
 *
 * Thread safety: none — external synchronisation required.
 */
class PasswordAuthenticator final : public IAuthenticator {
public:
  /** @brief Construct an empty credential store (all connections rejected). */
  PasswordAuthenticator() = default;

  /**
   * @brief Add or overwrite a username/password entry.
   *
   * @param username UTF-8 username string.
   * @param password Raw binary password; absent means no password required
   *                 for that username.
   */
  void add_credential(const Utf8String &username, const BinaryData &password);

  /**
   * @brief Remove a credential entry.
   *
   * No-op if the username is not present.
   *
   * @param username Username to remove.
   */
  void remove_credential(const Utf8String &username);

  /**
   * @brief Validate the credentials in the CONNECT packet (8.2.1 / 8.2.2).
   *
   * @param connect Decoded CONNECT packet.
   * @return `Success` when username and password match; `Failure /
   *         BadUserNameOrPassword` otherwise.
   */
  AuthResult authenticate(const ConnectPacket &connect) override;

  /**
   * @brief Validate enhanced-auth credentials from an AUTH packet
   * (8.3.3/8.3.4).
   *
   * Expects method `PLAIN` and an `AuthenticationData` payload encoded as
   * `username:password` in raw bytes.
   *
   * @param auth_pkt AUTH packet.
   * @return `Success` on valid credentials, otherwise `Failure` with an
   *         appropriate reason code.
   */
  AuthResult on_auth(const AuthPacket &auth_pkt) override;

private:
  /** @brief Maps username string value → expected password. */
  std::unordered_map<std::string, BinaryData> credentials_;
};

} // namespace mqtt
