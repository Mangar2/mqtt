#include "auth/enhanced_auth_handler.h"

#include "auth/auth_error.h"
#include <algorithm>
#include <format>

namespace mqtt {

//  Construction

EnhancedAuthHandler::EnhancedAuthHandler(
    std::shared_ptr<IAuthenticator> authenticator)
    : authenticator_(std::move(authenticator)) {
  if (!authenticator_) {
    throw AuthException(AuthError::InvalidState,
                        "EnhancedAuthHandler: authenticator must not be null");
  }
}

//  Helpers
//

std::string EnhancedAuthHandler::extract_auth_method(
    const std::vector<Property> &properties) {
  const auto iter = std::ranges::find_if(properties, [](const Property &prop) {
    return prop.id == PropertyId::AuthenticationMethod;
  });
  if (iter == properties.end()) {
    return {};
  }
  const auto *str = std::get_if<Utf8String>(&iter->value);
  return (str != nullptr) ? str->value : std::string{};
}

void EnhancedAuthHandler::validate_method(
    const std::string &method_in_pkt) const {
  if (method_in_pkt != auth_method_) {
    throw AuthException(
        AuthError::BadMethod,
        std::format(
            "EnhancedAuthHandler: method mismatch: expected '{}', got '{}'",
            auth_method_, method_in_pkt));
  }
}

//  Public API
//

AuthResult EnhancedAuthHandler::initiate(const ConnectPacket &connect) {
  if (state_ != EnhancedAuthState::Idle) {
    throw AuthException(
        AuthError::InvalidState,
        "EnhancedAuthHandler::initiate called in non-Idle state");
  }

  auth_method_ = extract_auth_method(connect.properties);
  if (auth_method_.empty()) {
    // Basic (non-enhanced) path: delegate directly.
    AuthResult result = authenticator_->authenticate(connect);
    state_ = (result.status == AuthStatus::Success)
                 ? EnhancedAuthState::Complete
                 : EnhancedAuthState::Failed;
    return result;
  }

  // Enhanced auth path.
  state_ = EnhancedAuthState::Authenticating;
  AuthResult result = authenticator_->authenticate(connect);

  if (result.status == AuthStatus::Success) {
    state_ = EnhancedAuthState::Complete;
  } else if (result.status == AuthStatus::Failure) {
    state_ = EnhancedAuthState::Failed;
  }
  // Continue: stay in Authenticating.
  return result;
}

AuthResult EnhancedAuthHandler::on_auth(const AuthPacket &pkt) {
  if (state_ != EnhancedAuthState::Authenticating &&
      state_ != EnhancedAuthState::Reauthenticating) {
    throw AuthException(
        AuthError::InvalidState,
        "EnhancedAuthHandler::on_auth called outside an active exchange");
  }

  const std::string method_in_pkt = extract_auth_method(pkt.properties);
  validate_method(method_in_pkt);

  AuthResult result = authenticator_->on_auth(pkt);

  if (result.status == AuthStatus::Success) {
    state_ = EnhancedAuthState::Complete;
  } else if (result.status == AuthStatus::Failure) {
    state_ = EnhancedAuthState::Failed;
  }
  // Continue: keep current state.
  return result;
}

AuthResult EnhancedAuthHandler::reauthenticate(const AuthPacket &pkt) {
  if (state_ != EnhancedAuthState::Complete) {
    throw AuthException(
        AuthError::InvalidState,
        "EnhancedAuthHandler::reauthenticate called outside Connected state");
  }

  const std::string method_in_pkt = extract_auth_method(pkt.properties);
  validate_method(method_in_pkt);

  state_ = EnhancedAuthState::Reauthenticating;

  // Build a synthetic CONNECT carrying only the auth properties so the
  // authenticator can process re-auth without a full packet context.
  ConnectPacket synthetic;
  synthetic.properties = pkt.properties;

  AuthResult result = authenticator_->authenticate(synthetic);

  if (result.status == AuthStatus::Success) {
    state_ = EnhancedAuthState::Complete;
  } else if (result.status == AuthStatus::Failure) {
    state_ = EnhancedAuthState::Failed;
  }
  // Continue: stay in Reauthenticating.
  return result;
}

bool EnhancedAuthHandler::is_enhanced(const ConnectPacket &connect) {
  const std::string method = extract_auth_method(connect.properties);
  return !method.empty();
}

std::string_view EnhancedAuthHandler::auth_method() const noexcept {
  return auth_method_;
}

EnhancedAuthState EnhancedAuthHandler::state() const noexcept { return state_; }

} // namespace mqtt
