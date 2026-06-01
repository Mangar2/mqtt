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
class MessageTreeNode;
```

### Class `MessageTree`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageTree(MessageTreeConfig)` | configuration + time provider |
| `addData` | `void(const Message&)` | insert/update one topic node |
| `getSection` | `vector<MessageTreeNode>(const string&, uint32_t, bool, bool) const` | prefix + depth query |
| `getNodes` | `vector<MessageTreeNode>(const vector<MessageTreeSnapshotNode>&, bool, bool) const` | returns changed nodes for provided snapshot topics only |
| `cleanup` | `size_t(uint32_t)` | removes stale nodes older than N days |
| `replaceAllNodes` | `void(const vector<MessageTreeNode>&)` | replaces full tree from persisted snapshot |
| `compressionStats` | `CompressionStats() const` | returns internal history compression counters |

### Class `MessageTreePersistence`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageTreePersistence(Config)` | persistence runtime config |
| `persistNow` | `bool(const MessageTree&)` | serializes tree to timestamped file |
| `restoreLatest` | `bool(MessageTree&)` | loads newest valid persisted file |
| `startPeriodic` | `void(const MessageTree&)` | starts background periodic persist loop |
| `stopPeriodic` | `void()` | stops periodic loop |

### Class `StringDirectory`

| Member | Signature | Notes |
|--------|-----------|-------|
| `add` | `uint16_t(const string&)` | adds unique text or returns existing slot index |
| `get` | `optional<string>(uint16_t) const` | returns non-empty slot value |
| `remove` | `bool(uint16_t)` | clears slot by setting it to empty string |
| `size` | `size_t() const` | counts non-empty slots |
| `capacity` | `size_t() const` | counts all vector slots, including empty slots |

### Class `MessageStore`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `MessageStore(MessageStoreConfig)` | builds tree+persistence from config |
| `getSubscriptions` | `SubscriptionMap() const` | returns configured topic->QoS map |
| `handleMessage` | `void(const Message&)` | cleanup-topic dispatch or tree addData |
| `storeMessageDirect` | `void(const Message&)` | direct tree addData only (no cleanup dispatch/logging) |
| `run` | `void()` | restore, start HTTP callback, start periodic persistence |
| `close` | `void()` | stop HTTP callback, stop periodic persistence, final persist |
| `querySection` | `vector<MessageTreeNode>(...) const` | read API used by future HTTP step |
| `queryNodes` | `vector<MessageTreeNode>(const vector<MessageTreeSnapshotNode>&, bool, bool) const` | snapshot diff read API |
| `queryCompressionStats` | `MessageTree::CompressionStats() const` | thread-safe internal compression counters |
| `persistSnapshotNow` | `optional<filesystem::path>()` | thread-safe immediate snapshot persist returning written path |

## Data behavior

- Configuration structs expose named default constants for key defaults
  (for example server port, history limits, retention count) to avoid literal coupling.

- Tree keys are topic path segments split by `/`.
- No per-node child-segment lookup cache as we have usually < 10 children
- `MessageTreeNode` and `MessageTreeHistoryEntry` are defined in dedicated files (`message_tree_node.h/.cpp`) as DTO-only types with plain `ReasonList` storage.
- Internal `TreeNode` storage types are defined in dedicated file `tree_node.h`.
- `TreeNode` uses per-node `StringDirectory` ownership for internal reason-message deduplication state.
- `StringDirectory` is not exposed through `MessageTreeNode` API; query DTOs remain free of internal compression details.
- Internal current/history reason lists store slot indices into the per-node `StringDirectory` (not duplicate message strings per reason entry).
- After history trimming and node updates, MessageTree compacts per-node reason-directory slots and remaps retained reason entries so unused slots are released.
- `StringDirectory` uses one `std::vector<std::string>` and linear search for duplicate detection and free-slot lookup.
- `StringDirectory::add` returns an existing slot index for duplicates; otherwise it reuses the first empty slot or appends at the end.
- `StringDirectory::remove` marks one slot as free by writing an empty string (`""`).
- `StringDirectory::size` counts only non-empty strings; `capacity` reports all slots including free ones.
- Every update moves previous `{timeMs,value,reason}` into history.
- Node timestamp source on `addData(message)`:
  - prefer `message.reason().front().timestamp` when it is a valid ISO-8601 timestamp with timezone,
  - otherwise fallback to current wall-clock from `nowMillisecondsProvider`.
- Tree and persistence keep timestamps internally as Unix epoch milliseconds (`timeMs`).
- MessageTree stores node/history reason timestamps internally in compact numeric form
  (`int64` epoch milliseconds).
- `timestampMs == 0` means "no reason timestamp".
- Reason timestamps are persisted and emitted in canonical UTC `Z` form.
- Fractional second precision is preserved (for example `...Z` vs `...000Z` behavior)
  without storing timezone offsets.
- History is compressed internally with original MessageTree-compatible entry types:
  - `single`: one `{time,value,reason}` entry.
  - `timeValue`: multiple `{time,value}` entries with equal reason-message chain.
  - `time`: multiple timestamps with one shared value and equal reason-message chain.
  - `interval`: regular updates with one shared value, represented by `{firstTime,lastTime,amount}`.
- Compression grouping uses reason message text equality (timestamp differences in reasons do not break grouping).
- Internal compressed history order is newest-first.
- Internal compressed history storage uses contiguous `std::vector` to minimize container overhead and to allow
  explicit capacity shrink after history trim.
- History is decompressed for output APIs in newest-first order.
- Compression transitions from `time` to `interval` must not overlap timestamps between entries;
  for monotonic input timestamps, each historic timestamp appears at most once in decompressed history.
- `getSection(..., includeHistory=true, includeReason=false)` keeps history reasons unchanged (legacy behavior);
  the flag only removes node-level `reason`.
- `interval` decompression follows legacy behavior and emits a synthetic reason entry:
  `regular update, amount: <N>`.
- Interval decompression exposes `lastTime` as output history timestamp so regular updates remain visible
  as advancing history entries to polling clients.
- Bounded history policy:
  - trim triggers when compressed entry count reaches `maxHistoryLength`
  - trim target becomes `maxHistoryLength - historyHysterese` (minimum `1`)
- Additional compression tuning parameters are supported:
  - `lengthForFurtherCompression`
  - `upperBoundFactor`
  - `upperBoundAddInMilliseconds`
  - `lowerBoundFactor`
  - `lowerBoundSubInMilliseconds`
- Promotion of identical `timeValue` suffixes to `time`/`interval` requires strictly more
  entries than the effective interval threshold, so exactly-threshold sequences remain
  as explicit history entries.
- Legacy compatibility for `lengthForFurtherCompression` is preserved:
  - configured values `1` and `2` are coerced to `3`,
  - configured value `0` remains `0`.

## Query behavior

- `getSection(prefix, depth, includeHistory, includeReason)` returns flat nodes below prefix.
- Depth is relative to prefix (`0` means only prefix node).
- `getNodes(snapshot, includeHistory, includeReason)` iterates only snapshot entries (legacy JS behavior).
- Nodes that exist only on server side but are absent in snapshot are not returned.
- Snapshot equality checks value first; optional `time` mismatch also marks changed.
- `reason` is compared only when the snapshot entry explicitly provides `reason`.
- When snapshot `reason` is omitted, reason differences are ignored.

## Cleanup behavior

- `cleanup(daysWithoutUpdate)` removes data nodes older than the cutoff.
- Empty branch nodes are pruned recursively.
- Return value is number of removed data nodes.

## Persistence behavior

- File naming: `<filename>_<timestamp>.mtree` in configured directory.
- `persistNow` writes full tree snapshot in compressed internal tree form (`MTREE2`) without history decompression.
- `restoreLatest` scans candidate files newest-first and loads first valid snapshot.
- `restoreLatest` supports `MTREE2` direct compressed format only.
- Missing/corrupt files are handled silently; restore returns false and tree remains usable.
- Retention keeps newest `keepFiles` snapshots and deletes older files.
- Periodic mode persists every `interval` milliseconds; `interval == 0` disables periodic loop.

## Component behavior

- `getSubscriptions()` returns `config.subscriptions` unchanged.
- `config.replayLoadedStateFile` (optional): when non-empty, `run()` writes a replay JSONL dump after restore containing one canonical YAHA envelope line per logical stored message (history oldest->newest, then current per topic).
- `config.replayIncomingMessagesFile` (optional): when non-empty, `handleMessage()` appends each post-restore inbound message as canonical YAHA envelope JSONL line.
- `handleMessage()`:
  - cleanup topic: parse payload as days and call `tree.cleanup(days)`.
  - other topics: call `tree.addData(message)`.
- `storeMessageDirect()` always calls `tree.addData(message)` without cleanup-topic special handling.
- `queryCompressionStats()` exposes counts of compressed history bucket types (`single`, `timeValue`, `time`, `interval`) and represented logical history message counts.
- `queryCompressionStats()` splits represented `timeValue` entries by payload type:
  - `representedTimeValueStringCount`: number of string payloads inside `timeValue` buckets.
  - `representedTimeValueDoubleCount`: number of numeric payloads inside `timeValue` buckets.
- `queryCompressionStats()` additionally exposes reason-compression visibility metrics:
  - `totalReasonEntryCount`: all stored reason entries across current node reasons and compressed-history reason lists.
  - `totalDirectoryStringCount`: sum of unique reason-message strings per node (effective string-directory cardinality).
  - `reasonEntriesPerDirectoryString`: compression ratio `totalReasonEntryCount / totalDirectoryStringCount` (or `0` when denominator is `0`).
- `persistSnapshotNow()` writes one snapshot file immediately and returns the written path on success.
- Non-numeric cleanup payload emits one structured error log line (`message_store[error] op=cleanup ...`).
- `run()` restores latest persisted snapshot before serving.
- `run()` writes replay loaded-state dump before startup stats logging and before enabling post-restore incoming replay append.
- `run()` emits a compression-stats header after restore attempt (`message_store[stats] phase=start_after_restore`) followed by one aligned metric per line (`name : value`, name left-aligned, value right-aligned).
- Stats output includes reason/directory metrics (`reasonEntries.total`, `directories.strings`, `ratio.reasonPerDirectoryString`) and `timeValue` type split metrics (`represented.timeValue.string`, `represented.timeValue.double`).
- Stats output additionally includes heap metrics on glibc/Linux (`heap.arenaKB`, `heap.inUseKB`, `heap.freeKB`, `heap.fragmentationPct`) and emits `heap.stats.unavailable : platform_not_glibc_linux` on unsupported platforms.
- `run()` starts an internal periodic compression-stats logger that emits every 60 seconds (`phase=periodic_60s`) using the same multiline aligned metric format.
- `run()` emits structured restore error log when no valid snapshot is available.
- `run()` catches restore exceptions and emits structured error logs instead of terminating.
- `close()` always performs one final `persistNow` after periodic loop is stopped.
- `close()` disables post-restore incoming replay append before final stats/persist handling.
- `close()` stops periodic compression-stats logging and emits one stats header before final persist (`phase=stop_after_signal`) using the same multiline aligned metric format.
- `close()` emits structured error log when final persist fails.
- `close()` catches final persist exceptions and emits structured error logs instead of terminating.

## HTTP behavior

- `run()` starts an internal cpp-httplib server on `config.serverHost:config.serverPort`.
- HTTP listen failure emits one structured error log (`message_store[error] op=http_listen ...`).
- GET path: `<config.serverPath>/<topicPrefix>`; default `serverPath` is `/store`.
- POST path: same base path, intended for `sensor.php` compatibility payloads.
- OPTIONS path: same base path (`<config.serverPath>/<topicPrefix>`) for CORS preflight.
- CORS response headers for HTTP endpoints:
  - `Access-Control-Allow-Origin: *`
  - `Access-Control-Allow-Methods: GET, POST, OPTIONS`
  - `Access-Control-Allow-Headers: Content-Type, Authorization, X-Requested-With, history, levelamount, reason, time`
  - `Access-Control-Max-Age: 86400` on OPTIONS responses
- Headers:
  - `levelamount` (default 1)
  - `history` (default false)
  - `reason` (default true)
  - `time` (default true)
- Empty body -> section mode using `getSection`.
- JSON array body -> snapshot diff mode using `getNodes`.
- POST JSON object mode:
  - `topic` maps to topic prefix (`"/a/b"` normalized to `"a/b"`).
  - `history`, `reason`, and `time` accept string and JSON boolean literals (`"true"`/`true` enables; `"false"`/`false` disables).
  - `levelAmount` and legacy alias `levelamount` support integer number or integer string; invalid values fall back to 1.
  - `nodes` property activates snapshot diff mode only for non-empty payload values. Empty `[]` and `null` keep section query mode.
  - In snapshot diff mode, `levelAmount` is applied only when explicitly present in request payload;
    then snapshot nodes are filtered to `topic` prefix and relative depth before diff evaluation.
  - In snapshot diff mode, `history`, `reason`, and `time` flags still control response projection for changed nodes.
  - response for successfully parsed sensor-compatible POST body is wrapped as JSON object
    with `payload` array field for legacy `sensor.php` compatibility.
  - Invalid POST JSON falls back to section query defaults (legacy bridge behavior).
- Malformed body -> empty result array with status 200.
- Unknown path -> status 404 with `YahaError` payload code `YAHA_MESSAGE_STORE_HTTP_NOT_FOUND`.
- Invalid percent-encoding in topic prefix -> status 400 with `YahaError` payload code `YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING`.
- Response is JSON array with `application/json`.
- All HTTP JSON string fields (`topic`, string `value`, `time`, reason `message`, reason `timestamp`) use strict JSON escaping; ASCII control bytes below `0x20` are emitted as `\u00XX` escapes.
- HTTP JSON node shape uses projection flags:
  - node field `time` (string, ISO-8601 UTC) is included only when `time=true`,
  - `history[]` is included only when `history=true`,
  - `history[].time` (string, ISO-8601 UTC) is included only when `time=true`,
  - node field `reason` and `history[].reason` are included only when `reason=true`,
  - `reason[].timestamp` is passthrough from message reasons.
- `time` projection is independent from `reason` projection.
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
| `tree_node.h` | Internal TreeNode + NodeData + compressed history type declarations |
| `message_tree_node.h` | MessageTreeNode + MessageTreeHistoryEntry DTO declarations |
| `message_tree_node.cpp` | MessageTreeNode + MessageTreeHistoryEntry DTO implementation |
| `string_directory.h` | StringDirectory declarations |
| `string_directory.cpp` | StringDirectory implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/message_tree_test.cpp` | Unit tests |
| `test/message_tree_persistence_test.cpp` | Persistence unit tests |
| `test/message_store_test.cpp` | MessageStore component tests |
| `test/string_directory_test.cpp` | StringDirectory unit tests |
