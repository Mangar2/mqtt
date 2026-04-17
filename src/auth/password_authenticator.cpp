#include "auth/password_authenticator.h"

namespace mqtt {

void PasswordAuthenticator::add_credential(const Utf8String &username,
                                           const BinaryData &password) {
  credentials_[username.value] = password;
}

void PasswordAuthenticator::remove_credential(const Utf8String &username) {
  credentials_.erase(username.value);
}

AuthResult PasswordAuthenticator::authenticate(const ConnectPacket &connect) {
  AuthResult failure{
      AuthStatus::Failure, ReasonCode::BadUserNameOrPassword, {}};

  if (!connect.username.has_value()) {
    return failure;
  }

  const auto iter = credentials_.find(connect.username->value);
  if (iter == credentials_.end()) {
    return failure;
  }

  const BinaryData &expected = iter->second;
  if (!connect.password.has_value() || connect.password.value() != expected) {
    return failure;
  }

  return {AuthStatus::Success, ReasonCode::Success, {}};
}

} // namespace mqtt
