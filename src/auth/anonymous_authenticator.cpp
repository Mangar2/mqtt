#include "auth/anonymous_authenticator.h"

namespace mqtt {

AnonymousAuthenticator::AnonymousAuthenticator(AnonymousPolicy policy) noexcept
    : policy_(policy) {}

AuthResult
AnonymousAuthenticator::authenticate(const ConnectPacket & /*connect*/) {
  if (policy_ == AnonymousPolicy::Allow) {
    return {AuthStatus::Success, ReasonCode::Success, {}};
  }
  return {AuthStatus::Failure, ReasonCode::NotAuthorized, {}};
}

AnonymousPolicy AnonymousAuthenticator::policy() const noexcept {
  return policy_;
}

} // namespace mqtt
