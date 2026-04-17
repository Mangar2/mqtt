# src/auth — Authentication Module (Module 8)

Verifies client identity at connection time. Depends on `data_model/` (module 1) and
`connection/` (module 7 types indirectly via CONNECT/AUTH packet structs).

---

## Purpose

Provides a pluggable authentication framework that covers:
- Basic username/password authentication (MQTT 5.0 §4.8.2)
- Enhanced multi-step authentication via AUTH packets (MQTT 5.0 §4.12)
- Anonymous access (configurable allow/deny)

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `auth_error.h`               | 8   | `AuthError` enum and `AuthException` |
| `authenticator.h`            | 8.1 | `AuthStatus`, `AuthResult`, `IAuthenticator` base class, `CallbackAuthenticator` |
| `anonymous_authenticator.h`  | 8.4 | `AnonymousAuthenticator` — always-allow or always-deny |
| `password_authenticator.h`   | 8.2 | `PasswordAuthenticator` — username/password credential store |
| `password_authenticator.cpp` | 8.2 | Implementation of credential lookup and validation |
| `enhanced_auth_handler.h`    | 8.3 | `EnhancedAuthHandler` — AUTH packet state machine |
| `enhanced_auth_handler.cpp`  | 8.3 | Implementation of multi-step handshake and re-authentication |

---

## Public API

### AuthResult (`authenticator.h`)

```cpp
enum class AuthStatus : uint8_t { Success, Continue, Failure };

struct AuthResult {
    AuthStatus                status;
    ReasonCode                reason_code;   // Success, ContinueAuthentication, or error
    std::optional<BinaryData> auth_data;     // Payload for AUTH Continue responses
};
```

### IAuthenticator (`authenticator.h`)

Abstract base class. Concrete implementations override `authenticate()` and
optionally `on_auth()`.

```cpp
class IAuthenticator {
public:
    virtual ~IAuthenticator() = default;
    virtual AuthResult authenticate(const ConnectPacket& connect) = 0;
    virtual AuthResult on_auth(const AuthPacket& auth_pkt);  // default: Failure
};
```

### CallbackAuthenticator (`authenticator.h`)

Wraps a `std::function<AuthResult(const ConnectPacket&)>` for lightweight plugin
registration (plan 8.1.2). A second optional callback handles `on_auth()`.

### AnonymousAuthenticator (`anonymous_authenticator.h`)

Concrete `IAuthenticator`. Behaviour is governed by `AnonymousPolicy`:
- `Allow` — authenticate() always returns `AuthStatus::Success`.
- `Deny`  — authenticate() always returns `AuthStatus::Failure /
  ReasonCode::NotAuthorized`.

### PasswordAuthenticator (`password_authenticator.h/.cpp`)

Concrete `IAuthenticator`. Stores a set of `{username, password}` credential
entries. `authenticate()` inspects `ConnectPacket::username` and
`ConnectPacket::password` and returns Success or `BadUserNameOrPassword`.

- Credential set is supplied at construction time or via `add_credential()`.
- `password` in the ConnectPacket is `optional<BinaryData>`; raw bytes are
  compared directly (no hashing in this layer).
- Missing username → `BadUserNameOrPassword`.

### EnhancedAuthHandler (`enhanced_auth_handler.h/.cpp`)

Manages the AUTH packet exchange (plan 8.3). Wraps an `IAuthenticator` pointer
and drives the multi-step protocol.

States: `Idle → Authenticating → Complete | Failed`;
re-authentication uses `Reauthenticating` state.

Key methods:
```cpp
// Call when CONNECT is received. Returns Continue (send AUTH 0x18) or
// Success/Failure (decide CONNACK immediately).
AuthResult initiate(const ConnectPacket& connect);

// Call when AUTH packet is received during handshake (8.3.3 / 8.3.4).
AuthResult on_auth(const AuthPacket& pkt);

// Call when AUTH(0x19 ReAuthenticate) is received during an active session (8.3.5).
AuthResult reauthenticate(const AuthPacket& pkt);

// True if a CONNECT carried an Authentication Method property.
bool is_enhanced(const ConnectPacket& connect) const;

// Authentication method string negotiated during initiate().
std::string_view auth_method() const;
```

---

## Behaviour constraints

- `EnhancedAuthHandler::on_auth()` throws `AuthException(InvalidState)` if called
  before `initiate()` or after the exchange has completed.
- `EnhancedAuthHandler::reauthenticate()` throws `AuthException(InvalidState)` if
  the session is not in `Connected` (i.e., `initiate()` must have succeeded first).
- `CallbackAuthenticator` throws `AuthException(InvalidState)` if constructed with
  a null callback.
- `PasswordAuthenticator` returns `Failure / BadUserNameOrPassword` for any missing
  or non-matching credential.

---

## Thread safety

None. External synchronisation required for all classes.
