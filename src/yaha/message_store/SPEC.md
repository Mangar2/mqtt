# message_store — MessageTree Core

## Purpose

Provides the internal MessageTree data structure for MessageStore step 4. The tree stores
current topic state, bounded history, and supports section queries, snapshot diff queries,
and stale-node cleanup.

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

## Files

| File | Role |
|------|------|
| `message_tree.h` | Public declarations |
| `message_tree.cpp` | Implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_tree_test.cpp` | Unit tests |
