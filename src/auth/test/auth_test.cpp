#include <catch2/catch_test_macros.hpp>

#include "auth/anonymous_authenticator.h"
#include "auth/auth_error.h"
#include "auth/authenticator.h"
#include "auth/enhanced_auth_handler.h"
#include "auth/password_authenticator.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

// ── Helpers
// ───────────────────────────────────────────────────────────────────

namespace {

ConnectPacket make_connect(std::optional<Utf8String> username = {},
                           std::optional<BinaryData> password = {}) {
  ConnectPacket pkt;
  pkt.username = std::move(username);
  pkt.password = std::move(password);
  return pkt;
}

ConnectPacket make_connect_with_method(const std::string &method) {
  ConnectPacket pkt;
  pkt.properties.push_back(
      {PropertyId::AuthenticationMethod, Utf8String{method}});
  return pkt;
}

AuthPacket
make_auth_pkt(const std::string &method,
              ReasonCode reason = ReasonCode::ContinueAuthentication) {
  AuthPacket pkt;
  pkt.reason_code = reason;
  pkt.properties.push_back(
      {PropertyId::AuthenticationMethod, Utf8String{method}});
  return pkt;
}

BinaryData make_binary(const std::string &str) {
  BinaryData bin;
  bin.data.assign(str.begin(), str.end());
  return bin;
}

// Minimal concrete IAuthenticator that exposes the default on_auth().
class MinimalAuthenticator final : public IAuthenticator {
public:
  AuthResult authenticate(const ConnectPacket & /*connect*/) override {
    return {AuthStatus::Success, ReasonCode::Success, {}};
  }
  // on_auth() left as default (Failure).
};

} // anonymous namespace

// ── AuthException
// ─────────────────────────────────────────────────────────────

TEST_CASE("auth_exception_stores_error_code", "[auth]") {
  AuthException exc(AuthError::NotAuthorized, "not authorized");
  CHECK(exc.error() == AuthError::NotAuthorized);
}

TEST_CASE("auth_exception_message_accessible", "[auth]") {
  AuthException exc(AuthError::BadMethod, "bad");
  CHECK(std::string(exc.what()) == "bad");
}

// ── IAuthenticator default on_auth
// ────────────────────────────────────────────

TEST_CASE("iauth_default_on_auth_returns_failure", "[auth]") {
  MinimalAuthenticator auth;
  AuthPacket pkt;
  AuthResult result = auth.on_auth(pkt);
  CHECK(result.status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::NotAuthorized);
}

// ── CallbackAuthenticator
// ─────────────────────────────────────────────────────

TEST_CASE("callback_auth_null_callback_throws", "[auth]") {
  try {
    CallbackAuthenticator auth(nullptr);
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::InvalidState);
  }
}

TEST_CASE("callback_auth_invokes_authenticate", "[auth]") {
  CallbackAuthenticator auth([](const ConnectPacket &) {
    return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
  });
  ConnectPacket pkt;
  AuthResult result = auth.authenticate(pkt);
  CHECK(result.status == AuthStatus::Success);
  CHECK(result.reason_code == ReasonCode::Success);
}

TEST_CASE("callback_auth_invokes_on_auth", "[auth]") {
  CallbackAuthenticator auth(
      [](const ConnectPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      },
      [](const AuthPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      });
  AuthPacket pkt;
  AuthResult result = auth.on_auth(pkt);
  CHECK(result.status == AuthStatus::Continue);
  CHECK(result.reason_code == ReasonCode::ContinueAuthentication);
}

TEST_CASE("callback_auth_default_on_auth_failure", "[auth]") {
  CallbackAuthenticator auth([](const ConnectPacket &) {
    return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
  });
  AuthPacket pkt;
  AuthResult result = auth.on_auth(pkt);
  CHECK(result.status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::NotAuthorized);
}

// ── AnonymousAuthenticator
// ────────────────────────────────────────────────────

TEST_CASE("anon_allow_returns_success", "[auth]") {
  AnonymousAuthenticator auth(AnonymousPolicy::Allow);
  AuthResult result = auth.authenticate(make_connect());
  CHECK(result.status == AuthStatus::Success);
  CHECK(result.reason_code == ReasonCode::Success);
}

TEST_CASE("anon_deny_returns_failure", "[auth]") {
  AnonymousAuthenticator auth(AnonymousPolicy::Deny);
  AuthResult result = auth.authenticate(make_connect());
  CHECK(result.status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::NotAuthorized);
}

TEST_CASE("anon_policy_getter_allow", "[auth]") {
  AnonymousAuthenticator auth(AnonymousPolicy::Allow);
  CHECK(auth.policy() == AnonymousPolicy::Allow);
}

TEST_CASE("anon_policy_getter_deny", "[auth]") {
  AnonymousAuthenticator auth(AnonymousPolicy::Deny);
  CHECK(auth.policy() == AnonymousPolicy::Deny);
}

TEST_CASE("anon_allow_ignores_credentials", "[auth]") {
  AnonymousAuthenticator auth(AnonymousPolicy::Allow);
  ConnectPacket pkt = make_connect(Utf8String{"user"}, make_binary("secret"));
  AuthResult result = auth.authenticate(pkt);
  CHECK(result.status == AuthStatus::Success);
}

// ── PasswordAuthenticator
// ─────────────────────────────────────────────────────

TEST_CASE("pw_auth_no_credentials_rejects", "[auth]") {
  PasswordAuthenticator auth;
  AuthResult result = auth.authenticate(make_connect());
  CHECK(result.status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::BadUserNameOrPassword);
}

TEST_CASE("pw_auth_missing_username_rejects", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"user"}, make_binary("pass"));
  AuthResult result = auth.authenticate(make_connect()); // no username
  CHECK(result.status == AuthStatus::Failure);
}

TEST_CASE("pw_auth_unknown_user_rejects", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"user"}, make_binary("pass"));
  AuthResult result =
      auth.authenticate(make_connect(Utf8String{"other"}, make_binary("pass")));
  CHECK(result.status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::BadUserNameOrPassword);
}

TEST_CASE("pw_auth_wrong_password_rejects", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"user"}, make_binary("correct"));
  AuthResult result =
      auth.authenticate(make_connect(Utf8String{"user"}, make_binary("wrong")));
  CHECK(result.status == AuthStatus::Failure);
}

TEST_CASE("pw_auth_missing_password_rejects", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"user"}, make_binary("pass"));
  AuthResult result =
      auth.authenticate(make_connect(Utf8String{"user"}, std::nullopt));
  CHECK(result.status == AuthStatus::Failure);
}

TEST_CASE("pw_auth_correct_credentials_accepts", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"alice"}, make_binary("s3cr3t"));
  AuthResult result = auth.authenticate(
      make_connect(Utf8String{"alice"}, make_binary("s3cr3t")));
  CHECK(result.status == AuthStatus::Success);
  CHECK(result.reason_code == ReasonCode::Success);
}

TEST_CASE("pw_auth_add_credential_and_authenticate", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"bob"}, make_binary("pwd"));
  AuthResult result =
      auth.authenticate(make_connect(Utf8String{"bob"}, make_binary("pwd")));
  CHECK(result.status == AuthStatus::Success);
}

TEST_CASE("pw_auth_remove_credential_rejects", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"dave"}, make_binary("xyz"));
  auth.remove_credential(Utf8String{"dave"});
  AuthResult result =
      auth.authenticate(make_connect(Utf8String{"dave"}, make_binary("xyz")));
  CHECK(result.status == AuthStatus::Failure);
}

TEST_CASE("pw_auth_remove_unknown_noop", "[auth]") {
  PasswordAuthenticator auth;
  // Should not throw.
  auth.remove_credential(Utf8String{"nobody"});
}

TEST_CASE("pw_auth_multiple_users", "[auth]") {
  PasswordAuthenticator auth;
  auth.add_credential(Utf8String{"alice"}, make_binary("aaa"));
  auth.add_credential(Utf8String{"bob"}, make_binary("bbb"));

  CHECK(auth.authenticate(make_connect(Utf8String{"alice"}, make_binary("aaa")))
            .status == AuthStatus::Success);
  CHECK(auth.authenticate(make_connect(Utf8String{"bob"}, make_binary("bbb")))
            .status == AuthStatus::Success);
}

// ── EnhancedAuthHandler
// ───────────────────────────────────────────────────────

TEST_CASE("enhanced_null_authenticator_throws", "[auth]") {
  try {
    EnhancedAuthHandler hdl(nullptr);
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::InvalidState);
  }
}

TEST_CASE("enhanced_initial_state_is_idle", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  CHECK(hdl.state() == EnhancedAuthState::Idle);
}

TEST_CASE("enhanced_is_enhanced_false_no_method", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt;
  CHECK(!hdl.is_enhanced(pkt));
}

TEST_CASE("enhanced_is_enhanced_true_with_method", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt = make_connect_with_method("PLAIN");
  CHECK(hdl.is_enhanced(pkt));
}

TEST_CASE("enhanced_auth_method_empty_before_initiate", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  CHECK(hdl.auth_method().empty());
}

TEST_CASE("enhanced_initiate_no_method_delegates_basic_success", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt;
  AuthResult result = hdl.initiate(pkt);
  CHECK(result.status == AuthStatus::Success);
  CHECK(hdl.state() == EnhancedAuthState::Complete);
}

TEST_CASE("enhanced_initiate_no_method_delegates_basic_failure", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Deny);
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt;
  AuthResult result = hdl.initiate(pkt);
  CHECK(result.status == AuthStatus::Failure);
  CHECK(hdl.state() == EnhancedAuthState::Failed);
}

TEST_CASE("enhanced_initiate_with_method_continue", "[auth]") {
  auto auth =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      });
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt = make_connect_with_method("SCRAM-SHA-256");
  AuthResult result = hdl.initiate(pkt);
  CHECK(result.status == AuthStatus::Continue);
  CHECK(hdl.state() == EnhancedAuthState::Authenticating);
}

TEST_CASE("enhanced_initiate_with_method_success", "[auth]") {
  auto auth =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      });
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt = make_connect_with_method("TOKEN");
  AuthResult result = hdl.initiate(pkt);
  CHECK(result.status == AuthStatus::Success);
  CHECK(hdl.state() == EnhancedAuthState::Complete);
}

TEST_CASE("enhanced_initiate_non_idle_throws", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt;
  hdl.initiate(pkt);
  try {
    hdl.initiate(pkt);
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::InvalidState);
  }
}

TEST_CASE("enhanced_on_auth_outside_exchange_throws", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  try {
    hdl.on_auth(make_auth_pkt("PLAIN"));
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::InvalidState);
  }
}

TEST_CASE("enhanced_on_auth_method_mismatch_throws", "[auth]") {
  auto auth = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      },
      [](const AuthPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM-SHA-256"));
  try {
    hdl.on_auth(make_auth_pkt("PLAIN")); // wrong method
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::BadMethod);
  }
}

TEST_CASE("enhanced_on_auth_continue_stays_authenticating", "[auth]") {
  auto auth = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      },
      [](const AuthPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  AuthResult result = hdl.on_auth(make_auth_pkt("SCRAM"));
  CHECK(result.status == AuthStatus::Continue);
  CHECK(hdl.state() == EnhancedAuthState::Authenticating);
}

TEST_CASE("enhanced_on_auth_success_completes", "[auth]") {
  auto auth = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      },
      [](const AuthPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  AuthResult result = hdl.on_auth(make_auth_pkt("SCRAM"));
  CHECK(result.status == AuthStatus::Success);
  CHECK(hdl.state() == EnhancedAuthState::Complete);
}

TEST_CASE("enhanced_on_auth_failure_fails", "[auth]") {
  auto auth = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      },
      [](const AuthPacket &) {
        return AuthResult{AuthStatus::Failure, ReasonCode::NotAuthorized, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  AuthResult result = hdl.on_auth(make_auth_pkt("SCRAM"));
  CHECK(result.status == AuthStatus::Failure);
  CHECK(hdl.state() == EnhancedAuthState::Failed);
}

TEST_CASE("enhanced_reauthenticate_outside_complete_throws", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  try {
    hdl.reauthenticate(make_auth_pkt("PLAIN", ReasonCode::ReAuthenticate));
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::InvalidState);
  }
}

TEST_CASE("enhanced_reauthenticate_method_mismatch_throws", "[auth]") {
  auto auth =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  try {
    hdl.reauthenticate(make_auth_pkt("PLAIN", ReasonCode::ReAuthenticate));
    FAIL("Expected AuthException");
  } catch (const AuthException &exc) {
    CHECK(exc.error() == AuthError::BadMethod);
  }
}

TEST_CASE("enhanced_reauthenticate_success", "[auth]") {
  auto auth =
      std::make_shared<CallbackAuthenticator>([](const ConnectPacket &) {
        return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  AuthResult result =
      hdl.reauthenticate(make_auth_pkt("SCRAM", ReasonCode::ReAuthenticate));
  CHECK(result.status == AuthStatus::Success);
  CHECK(hdl.state() == EnhancedAuthState::Complete);
}

TEST_CASE("enhanced_reauthenticate_continue", "[auth]") {
  int call_count = 0;
  auto auth = std::make_shared<CallbackAuthenticator>(
      [&call_count](const ConnectPacket &) {
        ++call_count;
        if (call_count == 1) {
          return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
        }
        return AuthResult{
            AuthStatus::Continue, ReasonCode::ContinueAuthentication, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  // First call returned Success, now Complete. Re-auth returns Continue.
  AuthResult result =
      hdl.reauthenticate(make_auth_pkt("SCRAM", ReasonCode::ReAuthenticate));
  CHECK(result.status == AuthStatus::Continue);
  CHECK(hdl.state() == EnhancedAuthState::Reauthenticating);
}

TEST_CASE("enhanced_auth_method_reflects_negotiated", "[auth]") {
  auto auth = std::make_shared<AnonymousAuthenticator>(AnonymousPolicy::Allow);
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM-SHA-256"));
  CHECK(hdl.auth_method() == "SCRAM-SHA-256");
}

TEST_CASE("enhanced_reauthenticate_failure", "[auth]") {
  int call_count = 0;
  auto auth = std::make_shared<CallbackAuthenticator>(
      [&call_count](const ConnectPacket &) {
        ++call_count;
        if (call_count == 1) {
          return AuthResult{AuthStatus::Success, ReasonCode::Success, {}};
        }
        return AuthResult{AuthStatus::Failure, ReasonCode::NotAuthorized, {}};
      });
  EnhancedAuthHandler hdl(auth);
  hdl.initiate(make_connect_with_method("SCRAM"));
  AuthResult result =
      hdl.reauthenticate(make_auth_pkt("SCRAM", ReasonCode::ReAuthenticate));
  CHECK(result.status == AuthStatus::Failure);
  CHECK(hdl.state() == EnhancedAuthState::Failed);
}

TEST_CASE("enhanced_initiate_with_method_failure", "[auth]") {
  auto auth = std::make_shared<CallbackAuthenticator>(
      [](const ConnectPacket &) {
        return AuthResult{AuthStatus::Failure, ReasonCode::NotAuthorized, {}};
      });
  EnhancedAuthHandler hdl(auth);
  ConnectPacket pkt = make_connect_with_method("SCRAM");
  AuthResult result = hdl.initiate(pkt);
  CHECK(result.status == AuthStatus::Failure);
  CHECK(hdl.state() == EnhancedAuthState::Failed);
}

} // namespace mqtt
