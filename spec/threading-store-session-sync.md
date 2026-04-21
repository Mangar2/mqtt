# Store Internal Synchronisation Plan

Vier Stores (`SubscriptionStore`, `SessionStore`, `SessionExpiryScheduler`,
`SessionTakeoverHandler`) besitzen keinen eigenen Lock. Sie werden von vier
verschiedenen Facades unter vier verschiedenen, voneinander unabhängigen Mutexes
aufgerufen — das bietet keine Mutual Exclusion zwischen den Facades. Jeder Store
bekommt einen eigenen internen Lock.

---

## Step 1 — SubscriptionStore

**Files:** `src/store/subscription_store.h`, `src/store/subscription_store.cpp`

`subscribers_for()` liegt auf dem Publish-Hotpath. Parallele Reads dürfen sich
nicht gegenseitig blockieren → `std::shared_mutex`.

| Method | Lock |
|--------|------|
| `store()` | `std::unique_lock<std::shared_mutex>` |
| `remove()` | `std::unique_lock<std::shared_mutex>` |
| `remove_session()` | `std::unique_lock<std::shared_mutex>` |
| `subscribers_for()` | `std::shared_lock<std::shared_mutex>` |
| `size()` | `std::shared_lock<std::shared_mutex>` |

```cpp
// subscription_store.h — private section
mutable std::shared_mutex mutex_;
```

Klassen-Kommentar updaten: "external synchronisation is required" → "all public methods are internally synchronised".

**Test:** `subscription_store_test.cpp` + TSAN.

---

## Step 2 — SessionStore

**Files:** `src/store/session_store.h`, `src/store/session_store.cpp`

```cpp
// session_store.h — private section
mutable std::mutex mutex_;
```

Alle öffentlichen Methoden: `std::lock_guard<std::mutex> lk(mutex_);`

Klassen-Kommentar updaten.

**Test:** `session_store_test.cpp` + TSAN.

---

## Step 3 — SessionExpiryScheduler

**Files:** `src/session_manager/session_expiry_scheduler.h`,
`src/session_manager/session_expiry_scheduler.cpp`

```cpp
// session_expiry_scheduler.h — private section
mutable std::mutex mutex_;
```

Alle öffentlichen Methoden: `std::lock_guard<std::mutex> lk(mutex_);`

"Thread safety: none" Kommentar updaten.

**Test:** `session_manager_test.cpp` + TSAN.

---

## Step 4 — SessionTakeoverHandler

**Files:** `src/session_manager/session_takeover_handler.h`,
`src/session_manager/session_takeover_handler.cpp`

```cpp
// session_takeover_handler.h — private section
mutable std::mutex mutex_;
```

Alle öffentlichen Methoden: `std::lock_guard<std::mutex> lk(mutex_);`

Achtung `takeover_if_exists()`: Der gespeicherte `close_callback` darf **nicht
unter dem Lock** aufgerufen werden (Deadlock-Gefahr). Callback unter Lock
herauskopieren, Lock freigeben, dann aufrufen.

"Thread safety: none" Kommentar updaten.

**Test:** `session_manager_test.cpp` + TSAN.

---

## Step 5 — Kommentar-Updates SessionManager / DisconnectFacade

**Files:** `src/session_manager/session_manager.h`, `src/broker/disconnect_facade.h`

- `SessionManager`: "Thread safety: none — external synchronisation required" entfernen. Nach Steps 1–4 sind alle genutzten Stores intern synchronisiert.
- `DisconnectFacade`: "Thread-safe disconnect facade" präzisieren — der eigene `mutex_` serialisiert die Sequenz Will-publish + unregister + session-remove pro Client; die Stores schützen sich selbst.

Keine Code-Änderungen, nur Kommentare.

---

## Acceptance Criteria

- [ ] `SubscriptionStore`, `SessionStore`, `SessionExpiryScheduler`, `SessionTakeoverHandler` — class-level comment "all public methods are internally synchronised"
- [ ] `SubscriptionStore` nutzt `std::shared_mutex`; Reads mit `shared_lock`
- [ ] Alle bestehenden Unit-Tests grün (kein Verhaltensunterschied)
- [ ] TSAN auf Debug-Build: keine Data-Race-Warnungen für die betroffenen Klassen unter concurrent subscribe + publish + disconnect
