#pragma once

/**
 * @file enhanced_auth_handler.h
 * @brief EnhancedAuthHandler — AUTH packet state machine for MQTT 5.0
 * enhanced authentication and re-authentication (Module 8.3).
 */

#include "auth/authenticator.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include <cstdint>
#include <memory>
#include <string>

namespace mqtt {

/**
 * @brief State of the enhanced authentication exchange.
 */
enum class EnhancedAuthState : uint8_t {
  Idle,             ///< No exchange in progress (initial state).
  Authenticating,   ///< Exchange started via `initiate()`; awaiting AUTH from
                    ///< client.
  Complete,         ///< Exchange completed successfully.
  Failed,           ///< Exchange ended with a failure.
  Reauthenticating, ///< Re-authentication in progress (post-connection).
};

/**
 * @brief Manages the MQTT 5.0 enhanced authentication exchange (Module 8.3).
 *
 * Drives the AUTH packet back-and-forth handshake on behalf of a connected
 * broker session. Delegates actual credential verification to an injected
 * `IAuthenticator`.
 *
 * Lifecycle for initial authentication:
 * 1. Receive CONNECT → call `initiate()`.
 *    - If the CONNECT carries no Authentication Method, `initiate()` returns
 *      immediately with the result of `IAuthenticator::authenticate()`.
 *    - If an Authentication Method is present, the handler stores it, calls
 *      `IAuthenticator::authenticate()`, and returns its result.
 * 2. While result is `Continue`, send `AUTH(0x18)` to the client and wait.
 * 3. On each client AUTH → call `on_auth()`, which forwards to
 *    `IAuthenticator::on_auth()`.
 * 4. When the authenticator returns `Success` or `Failure`, the exchange ends.
 *
 * Re-authentication (§4.12.1):
 * - Call `reauthenticate()` when an AUTH packet with `ReAuthenticate(0x19)` is
 *   received during an active (post-CONNACK) session.
 *
 * @throws AuthException(InvalidState) when methods are called in the wrong
 * state.
 * @throws AuthException(BadMethod) when the AUTH packet's method does not match
 *         the method negotiated in `initiate()`.
 *
 * Thread safety: none — external synchronisation required.
 */
class EnhancedAuthHandler {
public:
  /**
   * @brief Construct with an authenticator.
   *
   * @param authenticator Shared authenticator used for all auth steps.
   *                      Must not be null.
   * @throws AuthException(InvalidState) if `authenticator` is null.
   */
  explicit EnhancedAuthHandler(std::shared_ptr<IAuthenticator> authenticator);

  /**
   * @brief Start the authentication exchange from an incoming CONNECT packet
   * (8.3.1 / 8.3.2).
   *
   * If the CONNECT packet has no Authentication Method property, the handler
   * delegates directly to `IAuthenticator::authenticate()` without changing
   * state beyond `Complete` or `Failed`.
   *
   * If an Authentication Method is present, the state transitions to
   * `Authenticating` and the delegate's `authenticate()` is called.
   *
   * May only be called in state `Idle`.
   *
   * @param connect Decoded CONNECT packet.
   * @return `AuthResult` from the authenticator.
   * @throws AuthException(InvalidState) if state is not `Idle`.
   */
  AuthResult initiate(const ConnectPacket &connect);

  /**
   * @brief Process a client AUTH packet during the exchange (8.3.3 / 8.3.4).
   *
   * Forwards to `IAuthenticator::on_auth()`. Validates that the AUTH
   * packet's Authentication Method matches the method from `initiate()`.
   *
   * May only be called in state `Authenticating`.
   *
   * @param pkt Decoded AUTH packet from the client.
   * @return `AuthResult` from the authenticator.
   * @throws AuthException(InvalidState) if state is not `Authenticating`.
   * @throws AuthException(BadMethod) if the Authentication Method token
   *         in `pkt` does not match the one from `initiate()`.
   */
  AuthResult on_auth(const AuthPacket &pkt);

  /**
   * @brief Handle a re-authentication request during an active session
   * (8.3.5).
   *
   * The client sends an AUTH packet with `ReAuthenticate(0x19)`. The handler
   * transitions to `Reauthenticating` and delegates to
   * `IAuthenticator::authenticate()` via a synthetic ConnectPacket carrying
   * only the auth-method property.
   *
   * May only be called in state `Complete`.
   *
   * @param pkt AUTH packet carrying `ReAuthenticate` reason code.
   * @return `AuthResult` from the authenticator.
   * @throws AuthException(InvalidState) if state is not `Complete`.
   * @throws AuthException(BadMethod) if Authentication Method in `pkt` does
   *         not match the negotiated method.
   */
  AuthResult reauthenticate(const AuthPacket &pkt);

  /**
   * @brief Return true if the CONNECT packet carries an Authentication Method
   * property.
   * @param connect Decoded CONNECT packet to inspect.
   * @return `true` when Authentication Method is present and non-empty.
   */
  [[nodiscard]] static bool is_enhanced(const ConnectPacket &connect);

  /**
   * @brief Return the Authentication Method negotiated in `initiate()`.
   *
   * Empty string_view when `initiate()` has not been called or no method
   * was present in the CONNECT.
   *
   * @return View into the stored method string.
   */
  [[nodiscard]] std::string_view auth_method() const noexcept;

  /**
   * @brief Return the current state of the handler.
   * @return Current `EnhancedAuthState`.
   */
  [[nodiscard]] EnhancedAuthState state() const noexcept;

  /**
   * @brief Mark handler as connected with an already negotiated auth method.
   *
   * Used by runtime session code after CONNECT/Auth handshake already finished
   * in the broker phase. This enables valid AUTH(ReAuthenticate) handling in
   * connected-session flow.
   *
   * @param auth_method Negotiated enhanced auth method. Empty value leaves
   *        handler unchanged.
   * @throws AuthException(InvalidState) if called after state has advanced.
   */
  void bootstrap_connected_session(std::string auth_method);

private:
  /**
   * @brief Extract the UTF-8 Authentication Method from a property list.
   *
   * @param properties Property list to search.
   * @return The method string, or empty string if not present.
   */
  static std::string
  extract_auth_method(const std::vector<Property> &properties);

  /**
   * @brief Validate that packet's Authentication Method equals the
   * negotiated method.
   *
   * @param method_in_pkt Authentication Method found in the packet.
   * @throws AuthException(BadMethod) on mismatch.
   */
  void validate_method(const std::string &method_in_pkt) const;

  std::shared_ptr<IAuthenticator> authenticator_; ///< Injected authenticator.
  EnhancedAuthState state_{EnhancedAuthState::Idle};
  std::string auth_method_; ///< Negotiated method.
};

} // namespace mqtt
