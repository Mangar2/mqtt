#include "auth/authenticator.h"

#include "auth/auth_error.h"

namespace mqtt {

// ── IAuthenticator
// ────────────────────────────────────────────────────────────

AuthResult IAuthenticator::on_auth(const AuthPacket & /*auth_pkt*/) {
  return {AuthStatus::Failure, ReasonCode::NotAuthorized, {}};
}

// ── CallbackAuthenticator
// ─────────────────────────────────────────────────────

CallbackAuthenticator::CallbackAuthenticator(AuthenticateFn auth_fn,
                                             OnAuthFn on_auth_fn)
    : auth_fn_(std::move(auth_fn)), on_auth_fn_(std::move(on_auth_fn)) {
  if (!auth_fn_) {
    throw AuthException(
        AuthError::InvalidState,
        "CallbackAuthenticator: authenticate callback must not be null");
  }
}

AuthResult CallbackAuthenticator::authenticate(const ConnectPacket &connect) {
  return auth_fn_(connect);
}

AuthResult CallbackAuthenticator::on_auth(const AuthPacket &auth_pkt) {
  if (on_auth_fn_) {
    return on_auth_fn_(auth_pkt);
  }
  return IAuthenticator::on_auth(auth_pkt);
}

} // namespace mqtt
