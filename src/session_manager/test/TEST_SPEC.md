# test — Session Manager Tests (Module 10)

## Scope

Unit tests for all three sub-components:

- `SessionTakeoverHandler` (10.2) — connection tracking and takeover.
- `SessionExpiryScheduler` (10.3) — expiry timer scheduling and collection.
- `SessionManager` (10.1) — full integration of lifecycle, takeover, and expiry.

## Coverage Targets

| File | Target |
|------|--------|
| `session_takeover_handler.cpp` | ≥ 90 % |
| `session_expiry_scheduler.cpp` | ≥ 90 % |
| `session_manager.cpp`          | ≥ 90 % |

## Test Cases

### SessionTakeoverHandler

- Register and check `is_active`.
- Unregister removes entry.
- `takeover_if_exists` returns false when not active.
- `takeover_if_exists` invokes callback and returns true when active.
- `size` reflects registered connections.

### SessionExpiryScheduler

- `collect_expired` returns nothing when no timers scheduled.
- Interval == 0 is always expired once scheduled.
- Normal interval expires after deadline.
- Normal interval not expired before deadline.
- `never_expires` (0xFFFFFFFF) never returned.
- `cancel` removes entry from future collection.

### SessionManager

- `handle_connect` with empty client_id throws InvalidClientId.
- Clean start creates new session, session_present = false.
- Clean start removes existing session data.
- Resume with no existing session: session_present = false.
- Resume with existing session: session_present = true, expiry timer cancelled.
- Takeover: callback invoked, takeover_occurred = true.
- `handle_disconnect` with expiry = 0 removes session immediately.
- `handle_disconnect` with expiry > 0 schedules expiry timer.
- `handle_disconnect` expiry_override overrides stored expiry.
- `cleanup_expired` removes session data and returns client IDs.
- `cleanup_expired` leaves non-expired sessions intact.
- `inflight_store()` returns the same shared `InflightStore` reference used by the manager.
