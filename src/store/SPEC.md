# store — Module 4: In-Memory Store

Runtime state storage for the broker.
Depends on modules 1 (Data Models) and 3 (Topic Engine).

## Sub-modules

| File(s) | Plan ref | Description |
|---------|----------|-------------|
| `store_error.h`                                    | 4    | `StoreError` enum and `StoreException` |
| `subscription_store.h` / `subscription_store.cpp`  | 4.1  | In-memory subscription store; wraps `SubscriptionTrie` + `TopicMatcher` |
| `retained_message_store.h` / `retained_message_store.cpp` | 4.2 | In-memory map of retained messages keyed by topic name |
| `session_store.h` / `session_store.cpp`            | 4.3  | In-memory map of `SessionState` records keyed by client ID |
| `inflight_store.h` / `inflight_store.cpp`          | 4.4  | Per-session hash tables of `InflightEntry`; tracks in-use packet IDs and due outbound retransmits |

## SubscriptionStore (4.1)

Wraps `SubscriptionTrie` (Module 3.2) and `TopicMatcher` (Module 3.3).

| Method | Description |
|--------|-------------|
| `store(client_id, sub)` | Insert or overwrite a subscription for a client (4.1.1). |
| `remove(client_id, topic_filter)` | Remove the exact named subscription (4.1.2). |
| `subscribers_for(topic_name)` | Return all `MatchResult` pairs matching a publish topic (4.1.3). |
| `remove_session(client_id)` | Remove all subscriptions for a client session (4.1.4). |
| `size()` | Total number of stored (client_id, filter) pairs. |

## RetainedMessageStore (4.2)

Keyed by topic name (`std::string`). Only messages with a non-empty payload are stored.

| Method | Description |
|--------|-------------|
| `store(msg)` | Store or overwrite a retained message. If `msg.payload` is empty, the entry is deleted (4.2.1 + 4.2.2). |
| `find(topic_filter)` | Return all stored messages whose topic names match the filter (4.2.3). Wildcard matching uses `TopicMatcher`. System topics (`$`-prefix) are excluded from wildcard results per §4.7.2. |
| `find_records(topic_filter)` | Return retained records (`message` + `stored_at`) for matching topics. Used by Module 12.4 to enforce Message Expiry Interval against retained delivery time. |
| `all()` | Return copies of all stored messages including system topics.  Use this for persistence snapshots. |
| `size()` | Number of currently stored retained messages. |

### Retained message lookup

`find(topic_filter)` works by building a temporary `SubscriptionTrie` containing
the given filter as a single dummy subscription and then calling
`TopicMatcher::match(temp_trie, stored_topic)` for each stored topic name.  The
`TopicMatcher` system-topic exclusion rules (§4.7.2) are automatically respected.

Each retained entry also stores the broker-side `stored_at` timestamp.
`find()` strips metadata and returns only `Message` copies, while
`find_records()` keeps metadata for expiry-aware delivery paths.

## SessionStore (4.3)

Keyed by `client_id` (string). Internally maintains a disconnect-time map for
session-expiry calculations.

| Method | Description |
|--------|-------------|
| `create(session)` | Insert a new session; throws `StoreException(SessionAlreadyExists)` if the client ID is already present (4.3.1). |
| `load(client_id)` | Return `std::optional<SessionState>`; empty if not found (4.3.2). |
| `remove(client_id)` | Delete the session record and its disconnect timestamp (4.3.3). |
| `mark_disconnected(client_id, timestamp)` | Record the time a session disconnected; used to compute when it expires. |
| `expired_sessions(now)` | Return all sessions whose expiry interval has elapsed (4.3.4). Sessions with `session_expiry_interval == 0` are treated as expired immediately. Sessions with `0xFFFF'FFFF` never expire. Sessions with no recorded disconnect time are excluded. |
| `all()` | Return copies of all stored sessions.  Used by the persistence layer to snapshot the full session set. |
| `size()` | Number of stored sessions. |
| `contains(client_id)` | True if a session exists for the given client ID. |

## InflightStore (4.4)

Keyed by `client_id + direction`. Entries are uniquely identified by
`(client_id, packet_id, direction)`.

Implementation shape: sharded session index (`64` shards by default),
per-session mutex, and two per-session hash tables (`Outbound` and
`Inbound`) implemented as `std::unordered_map<uint16_t, InflightEntry>`.
The outbound table also maintains a dirty retransmit index (min-heap of
`timestamp + packet_id + generation`) to fetch due retransmissions without
iterating all outbound entries.

For a concrete `(client_id, packet_id, direction)` lookup path, typical
complexity for create/update/remove/lookup is average O(1). Rehash events or
adversarial hash collision patterns can degrade to O(n) worst-case behavior.

| Method | Description |
|--------|-------------|
| `create(client_id, InflightEntry&&)` | Add a new inflight entry by move (4.4.1). |
| `create(client_id, const InflightEntry&)` | Compatibility overload; copies then delegates to move overload. |
| `update(client_id, packet_id, direction, new_state)` | Advance the state of an existing entry; throws `StoreException(PacketIdNotFound)` if not found (4.4.2). |
| `remove(client_id, packet_id, direction)` | Remove a completed entry; no-op if not found (4.4.3). |
| `with_entry(client_id, packet_id, direction, visitor)` | Visit one entry when present; returns false if absent. |
| `for_each(client_id, direction, visitor)` | Iterate all live entries for one direction without producing vector copies. |
| `for_each(client_id, visitor)` | Iterate all live entries of both directions for one session. |
| `snapshot_each_session(visitor)` | Iterate all live entries across all sessions for persistence snapshots. |
| `due_outbound_packet_ids(client_id, cutoff)` | Return outbound packet IDs whose stored timestamp is <= `cutoff` using dirty-index lazy stale-discard. |
| `is_packet_id_in_use(client_id, packet_id, direction)` | Check whether a packet ID is currently registered (4.4.5). |
| `size_for(client_id)` | Number of inflight entries for the given session. |
| `total_size()` | Approximate total inflight entries across all sessions (atomic counter). |
| `drop_session(client_id)` | Remove all inflight entries for one session and delete its slot from the sharded index. |

Thread safety: `InflightStore` public methods are internally synchronized.

## Error handling

`StoreException` (derived from `std::runtime_error`) is thrown for unrecoverable
state violations:

| Error | Thrown by |
|-------|-----------|
| `SessionAlreadyExists` | `SessionStore::create` when the client ID is already present. |
| `InvalidPacketId` | `InflightStore::create` when `packet_id == 0`. |
| `PacketIdAlreadyInUse` | `InflightStore::create` when `(client_id, packet_id, direction)` already exists. |
| `PacketIdNotFound` | `InflightStore::update` when no matching entry exists. |
