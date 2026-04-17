# TEST_SPEC.md — src/auth/test

Unit tests for the Authentication Module (Module 8).

Each entry: **test name | scenario | inputs | expected result**

---

## AuthException (auth_error.h)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `auth_exception_stores_error_code` | Construct and retrieve error | `AuthError::NotAuthorized`, msg | `error()` == `NotAuthorized` |
| `auth_exception_message_accessible` | what() returns msg | `AuthError::BadMethod`, "bad" | `what()` == "bad" |

---

## IAuthenticator default on_auth (authenticator.h)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `iauth_default_on_auth_returns_failure` | Default on_auth via concrete subclass | any AuthPacket | `Failure / NotAuthorized` |

---

## CallbackAuthenticator (authenticator.h)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `callback_auth_null_callback_throws` | Null auth_fn | null function | `AuthException(InvalidState)` |
| `callback_auth_invokes_authenticate` | authenticate() calls callback | callback returns Success | `Success / Success` |
| `callback_auth_invokes_on_auth` | on_auth() calls on_auth callback | callback returns Continue | `Continue / ContinueAuthentication` |
| `callback_auth_default_on_auth_failure` | on_auth() with no on_auth callback | empty on_auth_fn | `Failure / NotAuthorized` |

---

## AnonymousAuthenticator (anonymous_authenticator.h)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `anon_allow_returns_success` | Allow policy, any CONNECT | `AnonymousPolicy::Allow` | `Success / Success` |
| `anon_deny_returns_failure` | Deny policy, any CONNECT | `AnonymousPolicy::Deny` | `Failure / NotAuthorized` |
| `anon_policy_getter_allow` | policy() returns Allow | constructed with Allow | `AnonymousPolicy::Allow` |
| `anon_policy_getter_deny` | policy() returns Deny | constructed with Deny | `AnonymousPolicy::Deny` |
| `anon_allow_ignores_credentials` | credentials in CONNECT are ignored | Allow + username present | `Success` |

---

## PasswordAuthenticator (password_authenticator.h/.cpp)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `pw_auth_no_credentials_rejects` | Empty store, no username in CONNECT | empty store | `Failure / BadUserNameOrPassword` |
| `pw_auth_missing_username_rejects` | No username field | no username in ConnectPacket | `Failure / BadUserNameOrPassword` |
| `pw_auth_unknown_user_rejects` | Username not in store | unknown username | `Failure / BadUserNameOrPassword` |
| `pw_auth_wrong_password_rejects` | Correct username, wrong password | password mismatch | `Failure / BadUserNameOrPassword` |
| `pw_auth_missing_password_rejects` | Correct username, no password field | no password in ConnectPacket | `Failure / BadUserNameOrPassword` |
| `pw_auth_correct_credentials_accepts` | Matching username+password | exact match | `Success / Success` |
| `pw_auth_add_credential_and_authenticate` | Add credential then authenticate | valid pair | `Success` |
| `pw_auth_remove_credential_rejects` | Remove credential, then authenticate | removed entry | `Failure / BadUserNameOrPassword` |
| `pw_auth_remove_unknown_noop` | Remove non-existent credential | no entry to remove | no exception |
| `pw_auth_multiple_users` | Multiple stored users, each authenticates | two distinct user/pw pairs | both return `Success` |

---

## EnhancedAuthHandler (enhanced_auth_handler.h/.cpp)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `enhanced_null_authenticator_throws` | Null shared_ptr passed to ctor | nullptr | `AuthException(InvalidState)` |
| `enhanced_initial_state_is_idle` | After construction | — | `state() == Idle` |
| `enhanced_is_enhanced_false_no_method` | CONNECT lacks auth method | no AuthenticationMethod property | `is_enhanced()` == false |
| `enhanced_is_enhanced_true_with_method` | CONNECT has auth method | AuthenticationMethod present | `is_enhanced()` == true |
| `enhanced_auth_method_empty_before_initiate` | auth_method() before initiate | — | `auth_method()` == "" |
| `enhanced_initiate_no_method_delegates_basic_success` | No auth method, authenticator returns Success | anon Allow | `Success`, state `Complete` |
| `enhanced_initiate_no_method_delegates_basic_failure` | No auth method, authenticator returns Failure | anon Deny | `Failure`, state `Failed` |
| `enhanced_initiate_with_method_continue` | Auth method present, authenticator returns Continue | callback returns Continue | `Continue`, state `Authenticating` |
| `enhanced_initiate_with_method_success` | Auth method present, authenticator returns Success immediately | callback returns Success | `Success`, state `Complete` |
| `enhanced_initiate_non_idle_throws` | initiate() called twice | call twice | `AuthException(InvalidState)` |
| `enhanced_on_auth_outside_exchange_throws` | on_auth() before initiate | — | `AuthException(InvalidState)` |
| `enhanced_on_auth_method_mismatch_throws` | AUTH packet has different method | method != negotiated | `AuthException(BadMethod)` |
| `enhanced_on_auth_continue_stays_authenticating` | Authenticator returns Continue again | callback Continue | `Continue`, state `Authenticating` |
| `enhanced_on_auth_success_completes` | Authenticator returns Success | callback Success | `Success`, state `Complete` |
| `enhanced_on_auth_failure_fails` | Authenticator returns Failure | callback Failure | `Failure`, state `Failed` |
| `enhanced_reauthenticate_outside_complete_throws` | reauthenticate() when not Complete | state Idle | `AuthException(InvalidState)` |
| `enhanced_reauthenticate_method_mismatch_throws` | Re-auth AUTH packet has wrong method | method != negotiated | `AuthException(BadMethod)` |
| `enhanced_reauthenticate_success` | Re-auth returns Success | callback returns Success | `Success`, state `Complete` |
| `enhanced_reauthenticate_continue` | Re-auth returns Continue | callback returns Continue | `Continue`, state `Reauthenticating` |
| `enhanced_reauthenticate_failure` | Re-auth returns Failure | callback returns Failure | `Failure`, state `Failed` |
| `enhanced_initiate_with_method_failure` | Auth method present, authenticator returns Failure immediately | callback returns Failure | `Failure`, state `Failed` |
| `enhanced_auth_method_reflects_negotiated` | auth_method() after initiate | method = "SCRAM-SHA-256" | `auth_method()` == "SCRAM-SHA-256" |
