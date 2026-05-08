# message_store — MessageTree + Persistence + Component + HTTP

## Purpose

Provides MessageStore foundations for steps 4 to 7: internal MessageTree data structure,
persistence service, MessageStore component logic implementing IMqttComponent, and HTTP query
interface (GET + sensor-compatible POST) via cpp-httplib.
JSON request parsing for snapshot and sensor-compatible payloads is isolated in dedicated
parser helper files to keep component logic compact.
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
| `queryNodes` | `vector<MessageTreeNode>(const vector<MessageTreeSnapshotNode>&) const` | snapshot diff read API |

## Data behavior

- Configuration structs expose named default constants for key defaults
  (for example server port, history limits, retention count) to avoid literal coupling.

- Tree keys are topic path segments split by `/`.
- Every update moves previous `{timeMs,value,reason}` into history.
- Node timestamp source on `addData(message)`:
  - prefer `message.reason().front().timestamp` when it is a valid ISO-8601 timestamp with timezone,
  - otherwise fallback to current wall-clock from `nowMillisecondsProvider`.
- Tree and persistence keep timestamps internally as Unix epoch milliseconds (`timeMs`).
- History is compressed internally with original MessageTree-compatible entry types:
  - `single`: one `{time,value,reason}` entry.
  - `timeValue`: multiple `{time,value}` entries with equal reason-message chain.
  - `time`: multiple timestamps with one shared value and equal reason-message chain.
  - `interval`: regular updates with one shared value, represented by `{firstTime,lastTime,amount}`.
- Compression grouping uses reason message text equality (timestamp differences in reasons do not break grouping).
- Internal compressed history order is newest-first.
- History is decompressed for output APIs in newest-first order.
- Compression transitions from `time` to `interval` must not overlap timestamps between entries;
  for monotonic input timestamps, each historic timestamp appears at most once in decompressed history.
- `getSection(..., includeHistory=true, includeReason=false)` keeps history reasons unchanged (legacy behavior);
  the flag only removes node-level `reason`.
- `interval` decompression follows legacy behavior and emits a synthetic reason entry:
  `regular update, amount: <N>`.
- Bounded history policy:
  - trim triggers when compressed entry count reaches `maxHistoryLength`
  - trim target becomes `maxHistoryLength - historyHysterese` (minimum `1`)
- Additional compression tuning parameters are supported:
  - `lengthForFurtherCompression`
  - `upperBoundFactor`
  - `upperBoundAddInMilliseconds`
  - `lowerBoundFactor`
  - `lowerBoundSubInMilliseconds`
- Legacy compatibility for `lengthForFurtherCompression` is preserved:
  - configured values `1` and `2` are coerced to `3`,
  - configured value `0` remains `0`.

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

## HTTP behavior

- `run()` starts an internal cpp-httplib server on `config.serverHost:config.serverPort`.
- GET path: `<config.serverPath>/<topicPrefix>`; default `serverPath` is `/store`.
- POST path: same base path, intended for `sensor.php` compatibility payloads.
- Headers:
  - `levelamount` (default 1)
  - `history` (default false)
  - `reason` (default true)
- Empty body -> section mode using `getSection`.
- JSON array body -> snapshot diff mode using `getNodes`.
- POST JSON object mode:
  - `topic` maps to topic prefix (`"/a/b"` normalized to `"a/b"`).
  - `history` and `reason` accept string and JSON boolean literals (`"true"`/`true` enables; `"false"`/`false` disables).
  - `levelAmount` and legacy alias `levelamount` support integer number or integer string; invalid values fall back to 1.
  - `nodes` property activates snapshot diff mode only for non-empty payload values. Empty `[]` and `null` keep section query mode.
  - response for successfully parsed sensor-compatible POST body is wrapped as JSON object
    with `payload` array field for legacy `sensor.php` compatibility.
  - Invalid POST JSON falls back to section query defaults (legacy bridge behavior).
- Malformed body -> empty result array with status 200.
- Unknown path -> status 404 with `YahaError` payload code `YAHA_MESSAGE_STORE_HTTP_NOT_FOUND`.
- Invalid percent-encoding in topic prefix -> status 400 with `YahaError` payload code `YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING`.
- Response is JSON array with `application/json`.
- HTTP JSON node shape uses ISO UTC timestamps with trailing `Z`:
  - node field `time` (string, ISO-8601 UTC),
  - `history[].time` (string, ISO-8601 UTC),
  - `reason[].timestamp` passthrough from message reasons.
- `history[]` in HTTP responses is ordered newest-first (`history[0]` is the newest historic entry).
- HTTP response does not expose internal `timeMs` fields.

## Files

| File | Role |
|------|------|
| `message_tree.h` | Public declarations |
| `message_tree.cpp` | Core tree implementation (path traversal, query, lifecycle helpers) |
| `message_tree_compression.cpp` | Original MessageTree-compatible history compression implementation |
| `iso_timestamp_parser.h` | ISO-8601 timestamp parse/format helper declarations |
| `iso_timestamp_parser.cpp` | ISO-8601 timestamp parse/format helper implementation |
| `message_tree_persistence.h` | Persistence declarations |
| `message_tree_persistence.cpp` | Persistence implementation |
| `message_store.h` | MessageStore component declarations |
| `message_store.cpp` | MessageStore component implementation |
| `message_store_json_parser.h` | JSON parser helper declarations for HTTP request payloads |
| `message_store_json_parser.cpp` | JSON parser helper implementation for snapshot + sensor POST formats |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_tree_test.cpp` | Unit tests |
| `test/message_tree_persistence_test.cpp` | Persistence unit tests |
| `test/message_store_test.cpp` | MessageStore component tests |
