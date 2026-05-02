# Thread-Safety Inventory und Umsetzungsplan

## Inventory: Shared Objects ohne internen Lock

Alle folgenden Objekte werden in mindestens einer Facade verwendet, sind
broker-weit geteilt und dokumentieren "Thread safety: none".

| # | Klasse | Header | Zugriff aus Facades |
|---|--------|--------|---------------------|
| 1 | `SubscriptionStore` | `store/subscription_store.h` | Connect, Disconnect, Subscribe, Publish (via InboundPublishProcessor) |
| 2 | `SessionStore` | `store/session_store.h` | Connect, Disconnect, Subscribe (via Orchestrator) |
| 3 | `SessionExpiryScheduler` | `session_manager/session_expiry_scheduler.h` | Connect, Disconnect, TickHandler |
| 4 | `SessionTakeoverHandler` | `session_manager/session_takeover_handler.h` | Connect, Disconnect |
| 5 | `MessageRouter` | `message_router/message_router.h` | Publish, Disconnect (offline buffer), Subscribe (retained delivery) |
| 6 | `InboundPublishProcessor` | `message_router/inbound_publish_processor.h` | Publish (über MessageRouter) |
| 7 | `SharedSubscriptionDispatcher` | `message_router/shared_subscription_dispatcher.h` | Subscribe, Disconnect |
| 8 | `OfflineQueue` | `message_router/offline_queue.h` | Disconnect (buffer), MessageRouter |
| 9 | `WillPublisher` | `will_manager/will_publisher.h` | Connect, Disconnect, TickHandler |
| 10 | `WillStore` | `will_manager/will_store.h` | WillPublisher |
| 11 | `WillDelayTimer` | `will_manager/will_delay_timer.h` | WillPublisher, TickHandler |
| 12 | `AclEngine` | `authz/acl_engine.h` | Connect, Subscribe (via Orchestrator) |
| 13 | `SubscriptionOrchestrator` | `subscription_manager/subscription_orchestrator.h` | Subscribe |
| 14 | `RetainedMessageStore` | `store/retained_message_store.h` | Publish (via InboundPublishProcessor) |

**Bereits korrekt gesichert — kein Handlungsbedarf:**
- `InflightStore` — interner `std::mutex`
- `ActiveConnectionRegistry` — interner `std::shared_mutex`
- `EnhancedAuthRegistry` — interner `std::mutex`
- `StatisticsCollector` — `std::atomic` Counter
- `StructuredTracer` — interner `std::mutex`

---

## Singletons (als Parameter durchgeschleift)

Alle 14 Objekte oben sowie die bereits gesicherten werden in
`BrokerModuleFactory::create()` als `unique_ptr` erzeugt, dann per Referenz
durch Konstruktoren aller Facades geschleift. Funktional sind das Singletons.

Die folgenden sollen eine `getInstance()` statische Methode bekommen und aus
allen Konstruktorparametern herausfallen:

| # | Klasse | Begründung |
|---|--------|------------|
| 1 | `SubscriptionStore` | Einmalig erzeugt, von allen Facades genutzt |
| 2 | `SessionStore` | Einmalig erzeugt, von allen Facades genutzt |
| 3 | `SessionExpiryScheduler` | Einmalig erzeugt |
| 4 | `SessionTakeoverHandler` | Einmalig erzeugt |
| 5 | `MessageRouter` | Einmalig erzeugt, broker-weit |
| 6 | `InboundPublishProcessor` | Einmalig erzeugt |
| 7 | `SharedSubscriptionDispatcher` | Einmalig erzeugt, broker-weit |
| 8 | `OfflineQueue` | Einmalig erzeugt |
| 9 | `WillPublisher` | Einmalig erzeugt |
| 10 | `WillStore` | Einmalig erzeugt |
| 11 | `WillDelayTimer` | Einmalig erzeugt |
| 12 | `AclEngine` | Einmalig erzeugt |
| 13 | `SubscriptionOrchestrator` | Einmalig erzeugt |
| 14 | `RetainedMessageStore` | Einmalig erzeugt |
| 15 | `InflightStore` | Einmalig erzeugt |
| 16 | `ActiveConnectionRegistry` | Einmalig erzeugt |
| 17 | `EnhancedAuthRegistry` | Einmalig erzeugt |
| 18 | `StatisticsCollector` | Einmalig erzeugt |
| 19 | `StructuredTracer` | Einmalig erzeugt |

---

## Umsetzungsplan

### Phase A — Interne Locks (Objekte 1–14)

Jedes Objekt bekommt einen eigenen internen Lock. Danach können die
Facade-Mutexes entfallen.

#### A1 — SubscriptionStore (`std::shared_mutex`)

`subscribers_for()` / `size()` → `shared_lock`. Alle Writes → `unique_lock`.

```cpp
// private:
mutable std::shared_mutex mutex_;
```

#### A2 — SessionStore (`std::mutex`)

Alle Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A3 — SessionExpiryScheduler (`std::mutex`)

Alle Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A4 — SessionTakeoverHandler (`std::mutex`)

Alle Methoden → `lock_guard`.
`takeover_if_exists()`: Callback unter Lock herauskopieren, Lock freigeben, dann aufrufen.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A5 — MessageRouter (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A6 — InboundPublishProcessor (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A7 — SharedSubscriptionDispatcher (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A8 — OfflineQueue (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A9 — WillPublisher (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.
Publish-Callback darf nicht unter Lock aufgerufen werden (ruft `MessageRouter` auf → Deadlock). Callback unter Lock herauskopieren, Lock freigeben.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A10 — WillStore (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A11 — WillDelayTimer (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A12 — AclEngine (`std::shared_mutex`)

`check()` ist read-only Hotpath → `shared_lock`. `reload()` → `unique_lock`.

```cpp
// private:
mutable std::shared_mutex mutex_;
```

#### A13 — SubscriptionOrchestrator (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

#### A14 — RetainedMessageStore (`std::mutex`)

Alle öffentlichen Methoden → `lock_guard`.

```cpp
// private:
mutable std::mutex mutex_;
```

---

### Phase B — Facade-Mutexes entfernen

Nach Phase A haben alle shared Objects eigene Locks. Die Facade-Mutexes
werden überflüssig:

| Facade | `mutex_` entfernen |
|--------|--------------------|
| `ConnectFacade` | Ja — `EnhancedAuthRegistry` hat eigenen Lock, `SessionManager` delegiert an gesicherte Stores |
| `DisconnectFacade` | Ja — alle aufgerufenen Objekte intern gesichert |
| `SubscribeFacade` | Ja — `SubscriptionOrchestrator` bekommt eigenen Lock (A13) |
| `PublishFacade` | Ja — `MessageRouter` bekommt eigenen Lock (A5), `StatisticsCollector` bereits atomic |

---

### Phase C — Singleton-Pattern

Jedes der 19 Objekte bekommt eine statische `getInstance()`-Methode.
`BrokerModuleFactory::create()` fällt weg oder wird auf Konfiguration reduziert.
Alle Konstruktorparameter in Facades, `SessionManager`, `SubscriptionOrchestrator`
etc. die auf diese Objekte zeigen werden durch `getInstance()`-Aufrufe ersetzt.

```cpp
// Beispiel-Pattern für jedes betroffene Objekt:
class SubscriptionStore {
public:
  static SubscriptionStore &getInstance();
private:
  static SubscriptionStore instance_;
};
```

Reihenfolge: Phase C erst nach Phase A abschliessen — die Objekte müssen
thread-safe sein bevor sie als Singletons global zugänglich gemacht werden.

---

## Acceptance Criteria

- [ ] Alle 14 Objekte aus Phase A haben einen internen Lock
- [ ] Alle Facade-Mutexes aus Phase B entfernt
- [ ] Alle 19 Objekte aus Phase C haben `getInstance()`
- [ ] `BrokerModuleFactory::create()` Parameterliste leer oder auf `BrokerConfig` reduziert
- [ ] TSAN auf Debug-Build ohne Data-Race-Warnungen
- [ ] Alle Unit-Tests grün
