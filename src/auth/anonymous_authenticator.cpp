#include "auth/anonymous_authenticator.h"

namespace mqtt {

AnonymousAuthenticator::AnonymousAuthenticator(AnonymousPolicy policy) noexcept
    : policy_(policy) {}

AuthResult
AnonymousAuthenticator::authenticate(const ConnectPacket & /*connect*/) {
  if (policy_ == AnonymousPolicy::Allow) {
    return {.status = AuthStatus::Success,
            .reason_code = ReasonCode::Success,
            .auth_data = {}};
  }
  return {.status = AuthStatus::Failure,
          .reason_code = ReasonCode::NotAuthorized,
          .auth_data = {}};
}

AnonymousPolicy AnonymousAuthenticator::policy() const noexcept {
  return policy_;
}

} // namespace mqtt
