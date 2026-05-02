# InflightStore Redesign — Per-Session Sharded Slot Arrays

Status: proposal
Module: 4.4 (`src/store/inflight_store.{h,cpp}`)
Related: `src/qos/qos1_state_machine.cpp`, `src/qos/qos2_state_machine.cpp`,
`src/client_session/client_session.cpp`,
`src/broker/persistence_coordinator.cpp`,
`src/session_manager/session_manager.cpp`

## 1. Problems with the current implementation

```cpp
class InflightStore {
  mutable std::mutex                                          mutex_;
  std::unordered_map<std::string, std::vector<InflightEntry>> entries_;
};
```

| # | Issue | Cost |
|---|-------|------|
| P1 | One global `std::mutex` for **every** session, both directions, all threads. | All QoS handshakes serialise through one critical section. |
| P2 | Per-session storage is `std::vector<InflightEntry>`. Every lookup (`find_entry`, `is_packet_id_in_use`, `remove`, `update`) is a linear scan. | O(n) per operation; `n` = inflight depth. |
| P3 | `entries_for(client_id)` returns a **copy of the whole vector**. `qos1_state_machine::retransmit`, `qos2_state_machine::retransmit`, `client_session::append_retransmission_frames`, `client_session::mark_session_resumed` and `persistence_coordinator` all call it. | Each call deep-copies every `InflightEntry` *including its `Message` and the full payload `BinaryData`*. With Receive-Maximum = 65 535 and 1 MiB payloads this is multiple GiB of churn. |
| P4 | `vector::push_back` / `vector::erase` reallocate the underlying buffer and shift tail elements. | Allocator pressure; cache-unfriendly. |
| P5 | `std::unordered_map` keyed by `std::string`. Every call constructs a fresh `std::string` from `string_view` for hashing and lookup. | Heap allocation on every read/write. |
| P6 | Memory never shrinks between bursts. `entries_.erase(map_iter)` only fires when the per-client vector empties; the vector capacity is never returned. | Long-lived sessions stay at peak footprint. |
| P7 | The store owns no knowledge of slot identity (packet_id ∈ [1, 65535]). It re-derives membership through linear scans even though `PacketIdManager` already keeps a parallel `std::unordered_set<uint16_t>`. | Duplicate state, double the work. |
| P8 | One container holds **inbound and outbound** entries mixed together. Direction is checked in every predicate. | Predicate work doubled; cache lines polluted. |

## 2. Design goals

| Goal | Requirement |
|------|-------------|
| G1   | All hot-path operations (`create`, `update`, `remove`, `is_packet_id_in_use`, lookup-by-packet-id) are O(1) worst case. |
| G2   | No global lock. Concurrent QoS handshakes on different sessions never block each other. |
| G3   | No deep copy of `Message` or its `payload` on `create`. The store takes ownership via move; subsequent reads expose the entry **by reference**. |
| G4   | No vector copy on iteration. Callers iterate via a callback (`for_each_outbound`, `for_each`) over only the *active* slots. |
| G5   | Memory grows on demand and **shrinks** when bursts subside. Empty allocation chunks are released back to the allocator (with a small per-session free list, mirroring the StreamBuffer redesign). |
| G6   | Allocation-light steady state: a chatty session of `k` inflight messages reuses the same `k / kSlotsPerChunk + 1` chunks indefinitely. |
| G7   | Bounded worst-case memory per session: `(65536 / kSlotsPerChunk) * sizeof(Chunk)` is the absolute upper bound, reached only at full Receive-Maximum saturation. |
| G8   | Persistence layer can iterate every entry of every session without forcing a global lock or producing copies. |
| G9   | Existing public semantics (errors, single-key uniqueness, direction independence) are preserved. |

## 3. Architecture overview

```
                 InflightStore  (singleton, no global lock)
                 ┌───────────────────────────────────────────┐
                 │  shard[0]   shard[1]   ...   shard[N-1]   │   N = power of two,
                 │   ┌───┐      ┌───┐           ┌───┐        │   default 64.
                 │   │ M │      │ M │           │ M │        │   M = shared_mutex
                 │   │ H │      │ H │           │ H │        │   H = open-addressed
                 │   └─┬─┘      └─┬─┘           └─┬─┘        │       hashmap
                 │     │          │               │          │       string_view → SessionSlot*
                 └─────┼──────────┼───────────────┼──────────┘
                       ▼          ▼               ▼
                 ┌──────────────────────────────────────────┐
                 │     SessionSlot (per client_id)          │
                 │     ┌──────────────────────────────┐     │
                 │     │ InflightTable<Outbound>      │     │
                 │     │ InflightTable<Inbound>       │     │
                 │     │ std::mutex slot_mutex_       │     │
                 │     └──────────────────────────────┘     │
                 └──────────────────────────────────────────┘
                                    │
                                    ▼
                 InflightTable      (per direction)
                 ┌──────────────────────────────────────────┐
                 │ chunks_[0..1023]   pid 1..64  (lazy)     │
                 │ chunks_[1]         pid 65..128 (lazy)    │
                 │ ...                                      │
                 │ free_list_                               │
                 │ active_count_                            │
                 │ active_chunk_count_                      │
                 │ first_active_chunk_, last_active_chunk_  │
                 └──────────────────────────────────────────┘
```

### 3.1 Sharded session registry (G2)

* The top-level `InflightStore` is a fixed array of `kShardCount`
  shards (default **64**, must be a power of two).
* Each shard owns one `std::shared_mutex` and one open-addressed
  hash map `client_id → SessionSlot*` (heterogeneous lookup, key is
  `std::string`, lookup key is `std::string_view`; **no per-call
  allocation**, G5/P5).
* Shard index is `hash(client_id) & (kShardCount - 1)`.
* Two unrelated sessions almost always hit different shards. Only
  session creation/removal needs the shard's `unique_lock`; all hot-path
  lookups take a `shared_lock` on the shard for the brief
  pointer-fetch and then operate on the per-session structure under
  that session's *own* mutex.

### 3.2 Per-session SessionSlot (G2, G6)

Each `SessionSlot` holds two independent `InflightTable`s, one per
`InflightDirection`. A small `std::mutex slot_mutex_` synchronises:

* updates from the connection thread that owns the session,
* persistence snapshots taken on the persistence-coordinator thread,
* session-takeover handoff,

without ever touching another session.

In the steady state every client is pinned to a single connection
executor thread (see `executor::ConnectionExecutor`); the per-session
mutex is uncontended and degenerates to a few atomic instructions.

### 3.3 Chunked slot table — `InflightTable` (G1, G5, G6, G7)

```cpp
constexpr std::size_t kSlotsPerChunk = 64;          // tuned to a uint64_t bitmap
constexpr std::size_t kChunkCount    = 65536 / 64;  // 1024 chunks per direction

struct alignas(64) Chunk {
    uint64_t      occupancy;                       // bit i set ⇒ slots[i] live
    std::uint16_t live_count;                      // popcount(occupancy)
    std::uint16_t base_pid;                        // first pid covered by this chunk
    Chunk*        prev_active;                     // active-chunk doubly-linked list
    Chunk*        next_active;
    std::aligned_storage_t<sizeof(InflightEntry),
                           alignof(InflightEntry)> slots[kSlotsPerChunk];
};

class InflightTable {
    std::array<Chunk*, kChunkCount> chunks_{};     // sparse, lazily filled
    Chunk*                          first_active_{nullptr};
    Chunk*                          last_active_{nullptr};
    Chunk*                          free_list_head_{nullptr};
    std::uint16_t                   free_list_size_{0};
    std::size_t                     active_count_{0};
    static constexpr std::uint16_t  kFreeListMax = 2;
};
```

* `packet_id` ∈ [1, 65535] maps directly to
  `(chunk_idx, slot_idx) = ((pid - 1) / 64, (pid - 1) % 64)` — pure
  arithmetic, O(1), no hashing (G1).
* Membership of a packet id in a chunk is the corresponding bit of
  `occupancy` — O(1), branchless test (G1, replaces P2 + P7).
* A chunk is allocated **only when the first slot in its 64-id range
  becomes live** (lazy growth, G5). A chunk is returned to the
  per-table free list when `live_count` drops to zero. The free list
  caps at `kFreeListMax` chunks; surplus chunks are `delete`d (G5
  shrink, mirrors the StreamBuffer free-list contract).
* `first_active_` / `last_active_` thread the **non-empty** chunks
  into a doubly-linked list. Iteration walks this list and visits at
  most `ceil(active_count_ / 64)` chunks — independent of the 65 535
  id space (G4, G7).

### 3.4 Why this shape?

* Direct array indexing beats hashing on this id space because the
  range is bounded and dense in practice.
* `uint64_t` bitmaps allow `__builtin_popcountll` and
  `__builtin_ctzll` for active-slot iteration in O(active) per chunk.
* Fixed-size chunks avoid every reallocation of P4. New chunks are
  one `operator new(sizeof(Chunk))` — and even that is amortised away
  by the free list (G6).
* Per-session ownership eliminates P1: a session cannot starve any
  other session.
* A 1024-pointer table per direction is **8 KiB** of pointers. With a
  typical session running with ≤ 16 inflight messages this is one
  chunk (≈ 4 KiB) plus the table — under 12 KiB. Compared to today's
  `std::vector<InflightEntry>` plus `std::unordered_map` overhead
  this is in the same order of magnitude with deterministic worst case
  (G7).

## 4. Public API

The store regains its singleton role but loses every linear method.
All callers continue to address it by `client_id`.

```cpp
namespace mqtt {

class InflightStore {
public:
    // (1) Create (G3 — entry is moved in)
    void create(std::string_view client_id, InflightEntry&& entry);

    // Compatibility overload, deprecated. Performs one move-construction.
    void create(std::string_view client_id, const InflightEntry& entry) {
        create(client_id, InflightEntry{entry});
    }

    // (2) Update — O(1), throws StoreException(PacketIdNotFound) on miss
    void update(std::string_view client_id, std::uint16_t packet_id,
                InflightDirection direction, InflightState new_state);

    // (3) Remove — O(1), no-op on miss
    void remove(std::string_view client_id, std::uint16_t packet_id,
                InflightDirection direction) noexcept;

    // (4) Membership test — O(1), no allocation
    [[nodiscard]] bool is_packet_id_in_use(
        std::string_view client_id, std::uint16_t packet_id,
        InflightDirection direction) const noexcept;

    // (5) Single-entry read by reference (G3, G4) — never copies the message.
    //     Returns false and leaves visit unchanged if no entry exists.
    template <class Visitor>
    bool with_entry(std::string_view client_id, std::uint16_t packet_id,
                    InflightDirection direction, Visitor&& visit) const;

    // (6) Iterate all live entries of one direction (G4) — no copy.
    //     Visitor receives `const InflightEntry&`.
    template <class Visitor>
    void for_each(std::string_view client_id,
                  InflightDirection direction, Visitor&& visit) const;

    // (7) Iterate both directions (used by retransmission scheduler).
    template <class Visitor>
    void for_each(std::string_view client_id, Visitor&& visit) const;

    // (8) Persistence snapshot helper. Calls the visitor under the
    //     per-session lock; the visitor must not call back into the store.
    template <class Visitor>
    void snapshot_each_session(Visitor&& visit) const;

    // (9) Counters
    [[nodiscard]] std::size_t size_for(std::string_view client_id) const noexcept;
    [[nodiscard]] std::size_t total_size() const noexcept; // approximate, lock-free

    // (10) Bulk drop on session takeover / clean-start
    void drop_session(std::string_view client_id) noexcept;
};

} // namespace mqtt
```

### 4.1 Removed methods

`std::vector<InflightEntry> entries_for(...)` is **deleted**. Every
call site is rewritten in terms of `for_each` or `with_entry`.

### 4.2 Caller migrations (G3, G4)

| Caller | Today | After |
|--------|-------|-------|
| `Qos1StateMachine::retransmit` | `entries_for` then linear `find_if` | `with_entry(..., InflightDirection::Outbound, [&](auto& e){...})` |
| `Qos2StateMachine::retransmit` | same | same |
| `ClientSession::append_retransmission_frames` | `entries_for` then full loop | `for_each(client_id_, InflightDirection::Outbound, ...)` |
| `ClientSession::mark_session_resumed` | `entries_for` then loop | `for_each(client_id_, [&](auto& e){ packet_id_manager_.register_existing(e.packet_id, e.direction); })` |
| `PersistenceCoordinator` snapshot | `entries_for` per session | `snapshot_each_session(...)` |

`Qos2StateMachine` currently does `remove(...); create(updated);` on
state transitions. That is replaced by a single `update(...)` plus
optional `update_state` extension when a field other than `state`
changes — no slot churn.

## 5. Hot-path algorithms

### 5.1 `create(client_id, std::move(entry))` — O(1)

```text
shard := shards_[hash(client_id) & (N-1)]
slot  := shard.lookup_or_create(client_id)        // open-addressed, heterogeneous
{ lock slot.slot_mutex_ }
table := slot.tables_[entry.direction]
pid   := entry.packet_id            (must be ≥ 1)
ci    := (pid - 1) >> 6
si    := (pid - 1) & 63
chunk := table.chunks_[ci]
if chunk == nullptr:
    chunk := table.acquire_chunk(ci)              // free list or new
    table.link_active(chunk)
mask  := uint64_t{1} << si
if chunk->occupancy & mask:                       // already in use
    throw StoreException(PacketIdAlreadyInUse, ...)
new (&chunk->slots[si]) InflightEntry(std::move(entry))
chunk->occupancy |= mask
chunk->live_count += 1
table.active_count_ += 1
```

### 5.2 `update / remove / is_packet_id_in_use / with_entry` — O(1)

All four operations dereference `chunks_[ci]` and test
`occupancy & (1ULL << si)`; on miss they short-circuit. On hit
`update` writes a single field, `remove` calls the destructor and
clears the bit, `with_entry` invokes the visitor on the live slot.

`remove` additionally:

```text
chunk->occupancy &= ~mask
chunk->live_count -= 1
table.active_count_ -= 1
if chunk->live_count == 0:
    table.unlink_active(chunk)
    table.release_chunk(chunk)        // free list or delete
```

### 5.3 `for_each(direction)` — O(active_count)

```text
for chunk = table.first_active_; chunk != nullptr; chunk = chunk->next_active:
    bits = chunk->occupancy
    while bits:
        si   = __builtin_ctzll(bits)
        bits &= bits - 1
        visit(*reinterpret_cast<InflightEntry*>(&chunk->slots[si]))
```

No copy, no allocation; pure pointer / bitmap walk (G4).

### 5.4 Free list (G5, mirrors StreamBuffer §5.4)

* `acquire_chunk(ci)`: pop free list LIFO, else `new Chunk{}`. Set
  `base_pid = ci * 64 + 1`. Zero the bitmap.
* `release_chunk(chunk)`: if `free_list_size_ < kFreeListMax`, push;
  else `delete`. Always run the slot destructors first (no leaks).
* `chunks_[ci] = nullptr` immediately after release so a future
  `create` for the same range allocates fresh and the lookup remains
  branchless.

### 5.5 `total_size()` — lock-free approximation

`total_size_` is an `std::atomic<std::size_t>` updated by
`create` / `remove`. Per-shard counters are also exposed for
diagnostics. The `session_manager` only uses `total_size()` for an
informational tag, so eventual consistency is acceptable.

## 6. Concurrency model (G2)

| Operation | Locks taken |
|-----------|-------------|
| Lookup of an existing session | `shared_lock` on shard; `unique_lock` on session slot. |
| Insert of a new session | `unique_lock` on shard; `unique_lock` on session slot. |
| Drop of a session | `unique_lock` on shard; session slot lock then dropped, slot deleted (use `std::unique_ptr` ownership in the shard map). |
| Persistence snapshot | For each shard: `shared_lock`; iterate session pointers, snapshot each session under its own slot lock. Multiple shards processed in parallel by the persistence coordinator. |

Two sessions on different shards never serialise. Two sessions on the
same shard serialise only for the duration of a hash-map lookup
(microseconds). Persistence and live traffic on disjoint sessions never
contend.

## 7. Memory behaviour (G5, G6, G7)

| Metric | Value |
|--------|-------|
| Per-direction overhead (table) | `kChunkCount * sizeof(Chunk*)` = 8 KiB |
| Per chunk overhead | 32 B header + `64 * sizeof(InflightEntry)` payload |
| Live `k` outbound entries (typical) | `ceil(k / 64)` chunks + 8 KiB table + ≤ `kFreeListMax` reserved chunks |
| Worst case (Receive-Max = 65 535, full) | 1024 chunks ≈ ≈ 1024 × `sizeof(Chunk)` |
| After burst drains to empty | only the free-listed chunks remain (default 2); no other allocator residue (G5) |
| Per-shard registry | 1× `shared_mutex` + small open-addressed table |

`InflightEntry` itself contains a `Message` whose `payload` is a
`std::vector<uint8_t>`. The redesign **moves** the entry into the slot;
the payload buffer ownership transfers without a copy, satisfying G3.
A future patch may further reduce per-entry footprint by replacing
`Message::payload` with a `std::shared_ptr<const std::vector<uint8_t>>`
when the same payload is fanned out to multiple sessions; that change
is **out of scope** for this redesign and stays compatible with the new
API.

## 8. Error handling

| Condition | Behaviour |
|-----------|-----------|
| `create` with `packet_id == 0` | `StoreException(InvalidPacketId)` (new code; today the bug is silent). |
| `create` for a `(client, pid, dir)` already live | `StoreException(PacketIdAlreadyInUse)` (today's vector silently appends a duplicate). |
| `update` on a missing entry | `StoreException(PacketIdNotFound)` (unchanged from today). |
| `remove` on a missing entry | no-op (unchanged). |
| `with_entry` / `for_each` on an unknown `client_id` | visitor not called; method returns. |

The two new error codes extend `StoreError`; callers in the QoS state
machines already operate inside try/catch boundaries that map
`StoreException` to disconnects, so propagation is straightforward.

## 9. Testing strategy

Unit tests under `src/store/test/inflight_store_test.cpp`:

| ID | Scenario | Assertion |
|----|----------|-----------|
| T1  | create / with_entry round trip | visitor sees identical entry; no copy (visitor address == slot address) |
| T2  | 65 535 outbound creates + 65 535 removes | `active_count_` returns to 0; chunk count returns to ≤ `kFreeListMax`; no leaks (custom allocator hook) |
| T3  | interleaved Inbound + Outbound at same packet_id | both coexist independently |
| T4  | remove of non-existent id | no-op, no exception |
| T5  | update of non-existent id | throws `PacketIdNotFound` |
| T6  | create duplicate `(client, pid, dir)` | throws `PacketIdAlreadyInUse` |
| T7  | for_each visits exactly the live ids in pid order | bitmap iteration produces ascending sequence |
| T8  | drop_session returns all chunks | no leaks, subsequent `size_for` == 0 |
| T9  | concurrent create/remove on 64 sessions × 8 threads | no data race (TSAN clean), `active_count_` consistent |
| T10 | concurrent traffic on session A and snapshot of session B | snapshot blocks neither A nor any third session |
| T11 | shrink behaviour: fill 1024 chunks then drain | chunk count drops to `kFreeListMax`; pointer table cleared |

Performance micro-benchmark (`tests/perf/inflight_store_bench.cpp`,
opt-in):

* `create` + `remove` round trip ≤ 30 ns median on the steady state
  (versus current vector-based ≥ 200 ns at depth 100).
* `for_each` over 1 000 entries scales linearly in the live count
  with no allocator activity.

Existing integration tests
(`load/combined_progressive_200_connections_with_timeout`,
`publish/fan_out_one_publisher_ten_subscribers`,
`shutdown/graceful/persistence_flushed_before_exit`) must continue
to pass; all relax thanks to G2.

## 10. Migration plan

1. **Add the new internals behind the existing header.**
   New private types: `Chunk`, `InflightTable`, `SessionSlot`,
   `Shard`. Public methods 1–4 stay source-compatible (`create`
   keeps both overloads); methods 5–10 are added.
2. **Rewrite `entries_for` callers** to use `with_entry` / `for_each`:
   * `src/qos/qos1_state_machine.cpp` (`retransmit`)
   * `src/qos/qos2_state_machine.cpp` (`retransmit`,
     state-mutation pair `remove + create` → `update`)
   * `src/client_session/client_session.cpp` (`mark_session_resumed`,
     `append_retransmission_frames`)
   * `src/broker/persistence_coordinator.cpp`
     (`snapshot_each_session`)
   * `src/session_manager/session_manager.cpp` (`total_size`
     unchanged).
3. **Remove `entries_for`** and the old `std::unordered_map` /
   `std::vector` storage.
4. **Add tests T1–T11** and the optional benchmark.
5. **Drop the single global `std::mutex`.** The old member is gone;
   contention metrics in the structured tracer should report the
   per-shard / per-session locks instead.

The redesign is internal to module 4.4 plus the five known callers.
No public ABI reaches the codec, the message router or the broker
facade.

## 11. Non-goals

* Lock-free read of slot data (would require versioned slots and
  hazard pointers; current callers are already pinned to one executor
  thread and the per-session mutex is fine).
* Sharing `Message::payload` by `shared_ptr` across sessions
  (separate optimisation, tracked elsewhere).
* Disk-backed inflight storage (handled by the persistence
  coordinator on top of this in-memory store).
* Adaptive chunk size (constant `kSlotsPerChunk = 64` keeps the
  bitmap a single `uint64_t`; revisit only if profiling shows the
  pointer table dominates footprint for tiny sessions).
