# session_manager — Session Manager (Module 10)

Controls session lifecycle and persistence coordination for MQTT 5.0 clients.
Depends on: `store/` (4), `connection/` (7), `auth/` (8).

## Responsibilities

- Create or resume a session when a client connects.
- Determine the `session_present` flag returned in CONNACK.
- Retain or discard session state when a client disconnects.
- Detect an existing connection with the same Client ID and displace it
  (session takeover, Reason Code 0x8E).
- Schedule and evaluate per-session expiry timers.
- Clean up all associated data (subscriptions, inflight entries) when a session
  expires or is discarded.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `session_manager_error.h`    | 10   | `SessionManagerError` enum and `SessionManagerException` |
| `session_open_result.h`      | 10.1 | `SessionOpenResult` — result returned by `SessionManager::handle_connect` |
| `session_takeover_handler.h/.cpp` | 10.2 | `SessionTakeoverHandler` — tracks active connections and closes the old connection on Client ID collision |
| `session_expiry_scheduler.h/.cpp` | 10.3 | `SessionExpiryScheduler` — records disconnect timestamps and reports expired sessions |
| `session_manager.h/.cpp`     | 10.1 | `SessionManager` — top-level coordinator for session lifecycle |

---

## Public API

### `SessionOpenResult` (session_open_result.h)

```cpp
struct SessionOpenResult {
    bool session_present;   // true when an existing session was resumed
    bool takeover_occurred; // true when an old connection was displaced
};
```

### `SessionManager` (session_manager.h)

```cpp
SessionManager(SessionStore&, SubscriptionStore&, InflightStore&,
               SessionTakeoverHandler&, SessionExpiryScheduler&);

SessionOpenResult handle_connect(const ConnectPacket& connect);
void handle_disconnect(std::string_view client_id,
                       std::optional<uint32_t> expiry_override,
                       std::chrono::steady_clock::time_point now);
std::vector<std::string> cleanup_expired(std::chrono::steady_clock::time_point now);
```

**`handle_connect`** (10.1.1–10.1.3, 10.2.1–10.2.3, 10.3.2):
1. Validates that `client_id` is non-empty; throws `SessionManagerException` otherwise.
2. Checks `SessionTakeoverHandler` for an existing active connection with the
   same Client ID; if found, closes it with Reason Code `SessionTakenOver`
   (0x8E) and sets `takeover_occurred = true`.
3. If `clean_start = true`: removes any existing session (and associated
   subscriptions and inflight entries) then creates a fresh session.
   Returns `session_present = false`.
4. If `clean_start = false`: tries to load an existing session.
   - Found → cancels the expiry timer, returns `session_present = true`.
   - Not found → creates a fresh session, returns `session_present = false`.
5. Registers the connection in `SessionTakeoverHandler`.

**`handle_disconnect`** (10.1.4, 10.3.1):
- Unregisters the connection from `SessionTakeoverHandler`.
- Computes the effective expiry interval (override takes precedence over the
  stored `session_expiry_interval`).
- Validates DISCONNECT expiry override semantics: a non-zero override is
  rejected when the stored session expiry is 0.
- If effective expiry == 0: immediately removes the session and all associated
  data.
- Otherwise: calls `SessionStore::mark_disconnected` and schedules the expiry
  in `SessionExpiryScheduler`.

**`is_disconnect_expiry_override_valid`**:
- Returns `false` when DISCONNECT tries to increase Session Expiry from 0 to
  non-zero.
- Returns `true` for absent override, override 0, and all non-conflicting
  overrides.

**`cleanup_expired`** (10.3.3):
- Queries `SessionStore::expired_sessions(now)`.
- For each expired session: removes all inflight entries, removes all
  subscriptions, removes the session record.
- Returns the list of cleaned-up Client IDs.

**`set_on_session_changed`** (13 — write-through persistence):
- Registers a `std::function<void()>` callback that is invoked after every
  mutation to session state (`handle_connect`, both paths of
  `handle_disconnect`, and `cleanup_expired` when at least one session was
  removed).
- The callback is never invoked when there are no changes.
- Exceptions from the callback are swallowed so they never propagate into
  the hot path.

```cpp
void set_on_session_changed(std::function<void()> callback) noexcept;
```

---

### `SessionTakeoverHandler` (session_takeover_handler.h)

```cpp
// Register an active connection, supplying a callback that closes it.
void register_connection(std::string_view client_id,
                         std::function<void()> close_callback);

// Unregister on disconnect (no-op if not registered).
void unregister_connection(std::string_view client_id);

// Close old connection if one exists.  Returns true on takeover.
bool takeover_if_exists(std::string_view client_id);

bool is_active(std::string_view client_id) const noexcept;
```

The `close_callback` is invoked immediately inside `takeover_if_exists` before
the new connection is registered.  After the callback the old entry is removed.

---

### `SessionExpiryScheduler` (session_expiry_scheduler.h)

```cpp
void schedule(std::string_view client_id,
              std::chrono::steady_clock::time_point disconnect_time,
              uint32_t expiry_interval);

void cancel(std::string_view client_id);

// Returns client IDs whose expiry deadline has passed.
std::vector<std::string> collect_expired(std::chrono::steady_clock::time_point now) const;
```

`expiry_interval == 0xFFFF'FFFF` means never expires; those entries are never
returned by `collect_expired`.

---

## Error Codes

| Code | Meaning |
|------|---------|
| `InvalidClientId` | `client_id` in CONNECT is empty |
