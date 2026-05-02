# Persistence Durability  Test Cases 19.4.3 and 19.4.4Plan 

## Problem Statement

Two integration tests fail because the broker's persistence layer is incomplete:

| Test | Failure | Root Cause |
|------|---------|------------|
| 19.4. inflight QoS1/2 resume after restart | Messages not delivered after clean restart | `OfflineQueue` is not included in the persistence flush |3 
| 19.4. crash recovery data integrity | Retained + queued messages lost after SIGKILL | Persistence flush only happens during clean shutdown; no write-through on mutations |4 

---

## Current Architecture (what already exists)

```
 .dat, backup .bak)
  OfflineQueue            in-memory per-client FIFO for disconnected sessions
                        NOT  completely lost on restart or crashpersisted 
```

SessionPersistence, RetainedMessagePersistence, InflightPersistence all exist.
PersistenceCoordinator::load() on startup, flush() only on clean shutdown.

### Why 19.4.3 fails (clean restart, inflight QoS1/2)

1. Session created, subscriber disconnects.
2. QoS1 + QoS2 messages published while subscriber is offline.
3. MessageRouter enqueues each to OfflineQueue.
4. Broker restarts cleanly (SIGTERM -> shutdown() -> flush()).

flush() saves sessions, retained, and InflightStore. It does NOT save OfflineQueue.
After restart the queued messages are gone. InflightPersistence only stores active
QoS handshake records (in-progress), not yet-to-be-started queued messages.

### Why 19.4.4 fails (crash recovery)

Broker killed with SIGKILL. shutdown() never called. flush() never called.
All data accumulated since startup-load is lost. Need write-through persistence.

---

## Required Changes

### Part  OfflineQueue Persistence (fixes 19.4.3)A 

####  New adapter: `OfflineQueuePersistence`A1 

Location: src/persistence/offline_queue_persistence.h/.cpp

```cpp
class OfflineQueuePersistence {
public:
    struct ClientMessages {
        std::string client_id;
        std::vector<Message> messages;  // FIFO order; enqueue_time not persisted
    };

    explicit OfflineQueuePersistence(std::filesystem::path dir);
    void save_all(const std::vector<ClientMessages> &entries);
    [[nodiscard]] std::vector<ClientMessages> load_all() const;
};
```

- Use CrashSafeFile with stem "offline_queue".
- enqueue_time is steady_ restore as steady_clock::now() on load (same pattern as InflightPersistence timestamps).clock 

####  Extend `OfflineQueue`A2 

Add to OfflineQueue:

```cpp
[[nodiscard]] std::unordered_map<std::string, std::vector<Message>> snapshot() const;
void restore(const std::string &client_id, const std::vector<Message> &messages);
```

####  Extend `PersistenceCoordinator`A3 

Add OfflineQueuePersistence and OfflineQueue parameters to load() and flush().

####  Wire into BrokerA4 

BrokerModuleFactory::create() creates OfflineQueuePersistence.
Broker holds offline_queue_persistence_ member and passes it to PersistenceCoordinator.

---

### Part  Write-Through Persistence (fixes 19.4.4)B 

Every mutation to persistent state must be flushed to disk synchronously.
Use injected std::function<void()> callbacks at mutation sites.
When persistence is disabled the callback is a no-op.

####  Write-through: Retained MessagesB1 

Mutation site: InboundPublishProcessor calls RetainedMessageStore::store() or remove().
Inject std::function<void()> on_retained_changed into InboundPublishProcessor.
After each retained mutation: retained_persistence.save_all(retained_store.all()).

####  Write-through: SessionsB2 

Mutation sites: SessionManager::handle_connect() and handle_disconnect().
Inject std::function<void()> on_session_changed into SessionManager.
After each mutation: session_persistence.save_all(session_store.all()).

####  Write-through: OfflineQueueB3 

Mutation sites: MessageRouter::route() (enqueue) and flush_offline_queue() (drain).
Inject std::function<void()> on_offline_queue_changed into MessageRouter.
After each enqueue/drain: offline_queue_persistence.save_all(...).

####  Write-through: InflightStoreB4 

Mutation sites: InflightStore::create(), update(),  called fromremove() 
QoS1StateMachine and QoS2StateMachine via ClientSession.
Inject std::function<void()> on_inflight_changed into ClientSession.
After each inflight mutation: inflight_persistence.save_all(...).

---

## Performance Note

Write-through triggers a full snapshot rewrite per mutation.
CrashSafeFile uses atomic  durable on POSIX after flush().rename() 
Acceptable tradeoff: correctness over throughput for this phase.
Future optimization (out of scope): append-only journal + compaction.

---

## Implementation Order

| Step | Task | Fixes |
|------|------|-------|
| 1 |  Create  |OfflineQueuePersistence | A1 
| 2 |  Extend OfflineQueue with snapshot/ |restore | A2 
| 3 | A3+ Wire into PersistenceCoordinator, Broker, BrokerModuleFactory | 19.4.3 |A4 
| 4 |  Write-through: retained  |messages | B1 
| 5 |  Write-through:  |sessions | B2 
| 6 |  Write-through: offline  |queue | B3 
| 7 |  Write-through: inflight entries | 19.4.4 |B4 

---

## Files to Create or Modify

| Action | File | Reason |
|--------|------|--------|
| Create | src/persistence/offline_queue_persistence.h | New persistence adapter |
| Create | src/persistence/offline_queue_persistence.cpp | Implementation |
| Modify | src/message_router/offline_queue.h | Add snapshot() and restore() |
| Modify | src/message_router/offline_queue.cpp | Implement new methods |
| Modify | src/broker/persistence_coordinator.h | Add OfflineQueue parameters |
| Modify | src/broker/persistence_coordinator.cpp | load/flush for offline queue |
| Modify | src/broker/broker.h | Add offline_queue_persistence_ member |
| Modify | src/broker/broker.cpp | Wire write-through callbacks |
| Modify | src/broker/broker_module_factory.h | Add OfflineQueuePersistence parameter |
| Modify | src/broker/broker_module_factory.cpp | Create OfflineQueuePersistence instance |
| Modify | src/message_router/message_router.h | Add on_queue_changed callback |
| Modify | src/message_router/message_router.cpp | Invoke callback after enqueue/drain |
| Modify | src/broker/connect_facade.h/.cpp | Inject + invoke on_session_changed |
| Modify | src/broker/disconnect_facade.h/.cpp | Inject + invoke on_session_changed |
| Modify | src/message_router/inbound_publish_processor.h/.cpp | Inject + invoke on_retained_changed |
| Modify | src/client_session/client_session.h/.cpp | Inject + invoke on_inflight_changed |
| Modify | src/persistence/SPEC.md | Document new module 13.4 |
| Modify | src/message_router/SPEC.md | Document snapshot/restore API |
| Update | src/persistence/test/TEST_SPEC.md | Test specs for OfflineQueuePersistence |

---

## Test Coverage Requirements

New unit tests needed:
- OfflineQueuePersistence round-trip (empty, one client, multiple clients)
- OfflineQueuePersistence timestamp reset on load
- OfflineQueuePersistence crash recovery (backup file fallback)
- OfflineQueue::snapshot() / restore() correctness
- PersistenceCoordinator::flush() includes offline queue
- PersistenceCoordinator::load() restores offline queue
- Write-through callback invoked on each retained/session/inflight mutation

All new/modified production files must reach >= 80% coverage before commit.
