# will_manager — Will Manager (Module 11)

Stores and publishes Will Messages according to MQTT 5.0 §3.1.3.1 / §4.13.
Depends on: `data_model/` (1), `store/` (4), `session_manager/` (10).

## Responsibilities

- Persist the Will Message that arrived with a CONNECT packet.
- Start a delay timer when the connection closes abnormally or with Reason 0x04.
- Cancel the timer when the client reconnects before the delay expires.
- Publish the will immediately when the session expires before the Will Delay fires.
- Suppress the will on a clean DISCONNECT (Reason 0x00).
- Publish the will by invoking a caller-supplied callback once the delay has elapsed.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `will_manager_error.h`    | 11   | `WillManagerError` enum and `WillManagerException` |
| `will_store.h/.cpp`       | 11.1 | `WillStore` — in-memory map of `WillMessage` records keyed by Client ID |
| `will_delay_timer.h/.cpp` | 11.2 | `WillDelayTimer` — tracks per-client disconnect timestamps and will-delay intervals |
| `will_publisher.h/.cpp`   | 11.3 | `WillPublisher` — orchestrates store, timer, and publish decisions |
| `will_message_util.h`     | 11.1 | Conversion helper from CONNECT `WillData` to internal `WillMessage` |

---

## Public API

### `WillManagerError` / `WillManagerException` (will_manager_error.h)

Currently no error conditions are defined; the header is reserved for future use.

---

### `WillStore` (will_store.h)

```cpp
class WillStore {
public:
    void store(std::string_view client_id, const WillMessage& will);
    std::optional<WillMessage> load(std::string_view client_id) const;
    void remove(std::string_view client_id);
    std::size_t size() const noexcept;
};
```

**`store`** (11.1.1): Inserts or replaces the `WillMessage` for `client_id`.

**`load`** (11.1.2): Returns the `WillMessage` for `client_id`, or `std::nullopt` if absent.

**`remove`** (11.1.3): Deletes the entry for `client_id`.  No-op when absent.

**`size`**: Returns the number of stored will entries.

---

### `WillDelayTimer` (will_delay_timer.h)

```cpp
class WillDelayTimer {
public:
    void schedule(std::string_view client_id,
                  std::chrono::steady_clock::time_point disconnect_time,
                  uint32_t delay_seconds);
    void cancel(std::string_view client_id);
    std::vector<std::string> collect_due(std::chrono::steady_clock::time_point now) const;
    std::size_t size() const noexcept;
};
```

**`schedule`** (11.2.1): Arms or replaces the delay timer for `client_id`.
If `delay_seconds == 0` the entry fires immediately (collected at or after `disconnect_time`).

**`cancel`** (11.2.2): Removes the delay-timer entry for `client_id`. No-op when absent.

**`collect_due`** (11.2.1): Returns all Client IDs whose `disconnect_time + delay_seconds ≤ now`.
Does **not** remove entries; call `cancel` after processing each.

**`size`**: Returns the number of pending timer entries.

---

### `WillPublisher` (will_publisher.h)

```cpp
class WillPublisher {
public:
    using PublishCallback = std::function<void(const WillMessage&)>;

    WillPublisher(WillStore& will_store,
                  WillDelayTimer& delay_timer,
                  PublishCallback publish_fn);

    void on_connect(std::string_view client_id, const WillMessage& will);
    void on_reconnect(std::string_view client_id);
    void on_disconnect(std::string_view client_id,
                       ReasonCode reason,
                       std::chrono::steady_clock::time_point now);
    void on_connection_lost(std::string_view client_id,
                            std::chrono::steady_clock::time_point now);
    void on_session_expired(std::string_view client_id);
    void publish_due(std::chrono::steady_clock::time_point now);
};
```

**`on_connect`** (11.1.1): Stores the will for `client_id`.

**`on_reconnect`** (11.2.2): Cancels any pending delay timer for `client_id`.

**`on_disconnect`** (11.3.2, 11.3.3):
- Reason `0x00` (Success / Normal Disconnection): removes the will and cancels any timer (suppress).
- Reason `0x04` (DisconnectWithWill): arms the delay timer using the stored will's `delay_interval`.
  If `delay_interval == 0`, publishes immediately and removes the will.
- Any other reason: same as `0x04`.

**`on_connection_lost`**: Arms the delay timer (same as non-zero disconnect reason).
If `delay_interval == 0`, publishes immediately and removes the will.

**`on_session_expired`** (11.2.3): If a pending will exists (in store and/or timer), publishes it
immediately and removes both the will and any pending timer entry.

**`publish_due`** (11.2.1, 11.3.1): Calls `collect_due` on the `WillDelayTimer`; for each
returned Client ID loads the will from `WillStore`, invokes `publish_fn`, and
removes both the will and the timer entry.

---

## Constraints

- Thread safety: none — external synchronisation required.
- `publish_fn` is called synchronously inside `publish_due` and `on_session_expired`.
- The module does not validate topic names or check authorisation; that is the
  responsibility of downstream components (Message Router, Module 12).
