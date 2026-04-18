#pragma once

/**
 * @file authenticator.h
 * @brief IAuthenticator abstract interface, AuthResult, and
 * CallbackAuthenticator plugin mechanism (Module 8.1).
 */

#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace mqtt {

/**
 * @brief Outcome status of a single authentication step.
 */
enum class AuthStatus : uint8_t {
  Success,  ///< Authentication complete; allow the connection.
  Continue, ///< Multi-step exchange in progress; send AUTH(0x18) to client.
  Failure,  ///< Authentication failed; deny the connection.
};

/**
 * @brief Result returned by every authentication step.
 *
 * - `status == Success`  → proceed with CONNACK Success.
 * - `status == Continue` → send AUTH packet with `reason_code ==
 *   ContinueAuthentication` and optional `auth_data` payload.
 * - `status == Failure`  → send CONNACK with the indicated `reason_code` and
 *   close the connection.
 */
struct AuthResult {
  AuthStatus status;      ///< Outcome of this step.
  ReasonCode reason_code; ///< Mapped reason code for the response packet.
  std::optional<BinaryData>
      auth_data; ///< Server-side auth payload (Continue only).
};

/**
 * @brief Convert UTF-8 password text to binary MQTT payload bytes.
 *
 * @param pass_word Password text.
 * @return BinaryData with one byte per input character.
 */
[[nodiscard]] inline BinaryData
binary_data_from_string(std::string_view pass_word) {
  BinaryData binary;
  binary.data.reserve(pass_word.size());
  for (char character : pass_word) {
    binary.data.push_back(static_cast<uint8_t>(character));
  }
  return binary;
}

/**
 * @brief Abstract base class for all MQTT 5.0 authenticators (Module 8.1.1).
 *
 * Concrete implementations override `authenticate()` for basic (single-step)
 * authentication from a CONNECT packet, and optionally `on_auth()` for
 * enhanced (multi-step) authentication driven by AUTH packets.
 *
 * Thread safety: none — external synchronisation required.
 */
class IAuthenticator {
public:
  /** @brief Virtual destructor. */
  virtual ~IAuthenticator() = default;

  /**
   * @brief Authenticate a client from the initial CONNECT packet.
   *
   * Called once per connection attempt. Implementations inspect
   * `connect.username`, `connect.password`, or the Authentication Method /
   * Authentication Data properties to decide the outcome.
   *
   * @param connect Decoded CONNECT packet.
   * @return `AuthResult` describing the outcome and any response payload.
   */
  virtual AuthResult authenticate(const ConnectPacket &connect) = 0;

  /**
   * @brief Process a client AUTH packet during a multi-step exchange (8.1).
   *
   * The default implementation returns `Failure / NotAuthorized`, indicating
   * that the authenticator does not support enhanced authentication.
   *
   * @param auth_pkt Decoded AUTH packet from the client.
   * @return `AuthResult` for the next step.
   */
  virtual AuthResult on_auth(const AuthPacket &auth_pkt);

protected:
  IAuthenticator() = default;
  IAuthenticator(const IAuthenticator &) = default;
  IAuthenticator &operator=(const IAuthenticator &) = default;
  IAuthenticator(IAuthenticator &&) = default;
  IAuthenticator &operator=(IAuthenticator &&) = default;
};

/**
 * @brief Callback-based authenticator plugin mechanism (Module 8.1.2).
 *
 * Wraps user-supplied `std::function` objects for `authenticate()` and
 * optionally `on_auth()`, allowing lightweight credential logic without
 * subclassing.
 *
 * @throws AuthException(InvalidState) if constructed with a null `authenticate`
 * callback.
 */
class CallbackAuthenticator final : public IAuthenticator {
public:
  /** @brief Callable type for the authenticate step. */
  using AuthenticateFn = std::function<AuthResult(const ConnectPacket &)>;

  /** @brief Callable type for the on_auth step (optional). */
  using OnAuthFn = std::function<AuthResult(const AuthPacket &)>;

  /**
   * @brief Construct with a mandatory authenticate callback and an optional
   * on_auth callback.
   *
   * @param auth_fn  Callback invoked by `authenticate()`. Must not be null.
   * @param on_auth_fn Callback invoked by `on_auth()`. May be null; the
   *                   default IAuthenticator::on_auth() behaviour is used
   *                   when absent.
   * @throws AuthException(InvalidState) if `auth_fn` is null.
   */
  explicit CallbackAuthenticator(AuthenticateFn auth_fn,
                                 OnAuthFn on_auth_fn = {});

  /**
   * @brief Invoke the registered authenticate callback.
   * @param connect Decoded CONNECT packet.
   * @return Result from the callback.
   */
  AuthResult authenticate(const ConnectPacket &connect) override;

  /**
   * @brief Invoke the registered on_auth callback, or fall back to the
   * default Failure result if no callback was provided.
   * @param auth_pkt Decoded AUTH packet.
   * @return Result from the callback or default Failure.
   */
  AuthResult on_auth(const AuthPacket &auth_pkt) override;

private:
  AuthenticateFn auth_fn_; ///< Mandatory authenticate callback.
  OnAuthFn on_auth_fn_;    ///< Optional on_auth callback.
};

} // namespace mqtt
