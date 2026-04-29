# message_store — MessageTree + Persistence + Component

## Purpose

Provides MessageStore foundations for steps 4 to 6: internal MessageTree data structure,
persistence service, and MessageStore component logic implementing IMqttComponent.
The tree stores current topic state, bounded history, section and snapshot diff queries,
plus stale-node cleanup. Persistence serializes tree state to disk, restores from the most
recent valid file on startup, and manages periodic saves.

## Public API

### Value types

```cpp
struct MessageTreeConfig;
struct MessageTreeHistoryEntry;
struct MessageTreeSnapshotNode;
struct MessageTreeNode;
```

### Class `MessageTree`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageTree(MessageTreeConfig)` | configuration + time provider |
| `addData` | `void(const Message&)` | insert/update one topic node |
| `getSection` | `vector<MessageTreeNode>(const string&, uint32_t, bool, bool) const` | prefix + depth query |
| `getNodes` | `vector<MessageTreeNode>(const vector<MessageTreeSnapshotNode>&) const` | returns changed nodes only |
| `cleanup` | `size_t(uint32_t)` | removes stale nodes older than N days |
| `replaceAllNodes` | `void(const vector<MessageTreeNode>&)` | replaces full tree from persisted snapshot |

### Class `MessageTreePersistence`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageTreePersistence(Config)` | persistence runtime config |
| `persistNow` | `bool(const MessageTree&)` | serializes tree to timestamped file |
| `restoreLatest` | `bool(MessageTree&)` | loads newest valid persisted file |
| `startPeriodic` | `void(const MessageTree&)` | starts background periodic persist loop |
| `stopPeriodic` | `void()` | stops periodic loop |

### Class `MessageStore`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageStore(MessageStoreConfig)` | builds tree+persistence from config |
| `getSubscriptions` | `SubscriptionMap() const` | returns configured topic->QoS map |
| `handleMessage` | `void(const Message&)` | cleanup-topic dispatch or tree addData |
| `run` | `void()` | restore, start HTTP callback, start periodic persistence |
| `close` | `void()` | stop HTTP callback, stop periodic persistence, final persist |
| `querySection` | `vector<MessageTreeNode>(...) const` | read API used by future HTTP step |

## Data behavior

- Tree keys are topic path segments split by `/`.
- Every update moves previous `{time,value,reason}` into history.
- History is compressed internally by grouping consecutive equal `value` and `reason`.
- History is decompressed for output APIs.
- Bounded history policy:
  - trim triggers when decompressed count > `maxHistoryLength`
  - trim target becomes `maxHistoryLength - historyHysterese`

## Query behavior

- `getSection(prefix, depth, includeHistory, includeReason)` returns flat nodes below prefix.
- Depth is relative to prefix (`0` means only prefix node).
- `getNodes(snapshot)` compares topic/value/reason against snapshot and returns changed/new nodes.

## Cleanup behavior

- `cleanup(daysWithoutUpdate)` removes data nodes older than the cutoff.
- Empty branch nodes are pruned recursively.
- Return value is number of removed data nodes.

## Persistence behavior

- File naming: `<filename>_<timestamp>.mtree` in configured directory.
- `persistNow` writes full tree snapshot including current values and history.
- `restoreLatest` scans candidate files newest-first and loads first valid snapshot.
- Missing/corrupt files are handled silently; restore returns false and tree remains usable.
- Retention keeps newest `keepFiles` snapshots and deletes older files.
- Periodic mode persists every `interval` milliseconds; `interval == 0` disables periodic loop.

## Component behavior

- `getSubscriptions()` returns `config.subscriptions` unchanged.
- `handleMessage()`:
  - cleanup topic: parse payload as days and call `tree.cleanup(days)`.
  - other topics: call `tree.addData(message)`.
- Non-numeric cleanup payload is ignored.
- `run()` restores latest persisted snapshot before serving.
- `close()` always performs one final `persistNow` after periodic loop is stopped.
- HTTP server integration is callback-based in step 6; concrete endpoint implementation comes in step 7.

## Files

| File | Role |
|------|------|
| `message_tree.h` | Public declarations |
| `message_tree.cpp` | Implementation |
| `message_tree_persistence.h` | Persistence declarations |
| `message_tree_persistence.cpp` | Persistence implementation |
| `message_store.h` | MessageStore component declarations |
| `message_store.cpp` | MessageStore component implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_tree_test.cpp` | Unit tests |
| `test/message_tree_persistence_test.cpp` | Persistence unit tests |
| `test/message_store_test.cpp` | MessageStore component tests |
